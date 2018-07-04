#include "RenderProcessHandler.h"

#define IMPLEMENT_LOGGER_METHOD( Name, SEVERITY )                \
void RenderProcessLogger::Name( const char *format, ... ) {      \
	va_list va;                                                  \
	va_start( va, format );                                      \
	SendLogMessage( SEVERITY, format, va );                      \
	va_end( va );                                                \
}

IMPLEMENT_LOGGER_METHOD( Debug, LOGSEVERITY_DEBUG )
IMPLEMENT_LOGGER_METHOD( Info, LOGSEVERITY_INFO )
IMPLEMENT_LOGGER_METHOD( Warning, LOGSEVERITY_WARNING )
IMPLEMENT_LOGGER_METHOD( Error, LOGSEVERITY_ERROR )

void RenderProcessLogger::SendLogMessage( cef_log_severity_t severity, const char *format, va_list va ) {
	char buffer[2048];
	vsnprintf( buffer, sizeof( buffer ), format, va );

	auto message( CefProcessMessage::Create( "log" ) );
	auto args( message->GetArgumentList() );
	args->SetString( 0, buffer );
	args->SetInt( 1, severity );
	browser->SendProcessMessage( PID_BROWSER, message );
}

void WswCefRenderProcessHandler::OnBrowserCreated( CefRefPtr<CefBrowser> browser ) {
	if( !logger.get() ) {
		auto newLogger( std::make_shared<RenderProcessLogger>( browser ) );
		logger.swap( newLogger );
	}
}

void WswCefRenderProcessHandler::OnBrowserDestroyed( CefRefPtr<CefBrowser> browser ) {
	if( logger->UsesBrowser( browser ) ) {
		std::shared_ptr<RenderProcessLogger> emptyLogger;
		logger.swap( emptyLogger );
	}
}

void WswCefRenderProcessHandler::OnWebKitInitialized() {
	const char *code =
		"var ui; if (!ui) { ui = {}; }"
		"(function() {"
		"	ui.notifyUiPageReady = function() {"
		"		native function notifyUiPageReady();"
		"		notifyUiPageReady();"
		"	};"
		"	ui.getCVar = function(name, defaultValue, callback) {"
		"   	native function getCVar(name, defaultValue, callback);"
		"		getCVar(name, defaultValue, callback);"
		"	};"
		"	ui.setCVar = function(name, value, callback) {"
		"		native function setCVar(name, value, callback);"
		"       setCVar(name, value, callback);"
		"	};"
		"	ui.executeNow = function(text, callback) {"
		"		native function executeCmd(whence, text, callback);"
		"		executeCmd('now', text, callback);"
		"	};"
		"	ui.insertToExecute = function(text, callback) {"
		"		native function executeCmd(whence, text, callback);"
		"		executeCmd('insert', text, callback);"
		"	};"
		"	ui.appendToExecute = function(text, callback) {"
		"		native function executeCmd(whence, text, callback);"
		"		executeCmd('append', text, callback);"
		"	};"
		"	ui.getVideoModes = function(callback) {"
		"		native function getVideoModes(callback);"
		"		/* Complex object are passed as a JSON string */"
		"		getVideoModes(function(result) { callback(JSON.parse(result)); });"
		"	};"
		"	ui.getDemosAndSubDirs = function(dir, callback) {"
		"		native function getDemosAndSubDirs(dir, callback);"
		"		/* Two arrays of strings are passed as strings */"
		"		getDemosAndSubDirs(dir, function(demos, subDirs) {"
		"			callback(JSON.parse(demos), JSON.parse(subDirs));"
		"		});"
		"	};"
		"	ui.getDemoMetaData = function(fullPath, callback) {"
		"		native function getDemoMetaData(fullPath, callback);"
		"		/* Complex objects are passed as a JSON string */"
		"		getDemoMetaData(fullPath, function(metaData) {"
		"			callback(JSON.parse(metaData));"
		"		});"
		"	};"
		"	ui.getHuds = function(callback) {"
		"		native function getHuds(callback);"
		"		/* Array of huds is passed as a string */"
		"		getHuds(function(hudsList) {"
		"			callback(JSON.parse(hudsList));"
		"		});"
		"	};"
		"	ui.getGametypes = function(callback) {"
		"		native function getGametypes(callback);"
		"		getGametypes(function(serialized) {"
		"			callback(JSON.parse(serialized));"
		"		});"
		"	};"
		"	ui.getMaps = function(callback) {"
		"		native function getMaps(callback);"
		"		getMaps(function(serialized) {"
		"			callback(JSON.parse(serialized));"
		"		});"
		"	};"
		"	ui.getLocalizedStrings = function(strings, callback) {"
		"		native function getLocalizedStrings(strings, callback);"
		"		getLocalizedStrings(strings, function(serializedObject) {"
		"			callback(JSON.parse(serializedObject));"
		"		});"
		"	};"
		"	ui.getKeyNames = function(keys, callback) {"
		"		native function getKeyNames();"
		"		getKeyNames(keys, function(serializedObject) {"
		"			callback(JSON.parse(serializedObject));"
		"		});"
		"	};"
		"	ui.getAllKeyNames = function(callback) {"
		"		native function getKeyNames();"
		"		getKeyNames(function(serializedObject) {"
		"			callback(JSON.parse(serializedObject));"
		"		});"
		"	};"
		"	ui.getKeyBindings = function(keys, callback) {"
		"		native function getKeyBindings();"
		"		getKeyBindings(keys, function(serializedObject) {"
		"			callback(JSON.parse(serializedObject));"
		"		});"
		"	};"
		"	ui.getAllKeyBindings = function(callback) {"
		"		native function getKeyBindings();"
		"		getKeyBindings(function(serializedObject) {"
		"			callback(JSON.parse(serializedObject));"
		"		});"
		"	};"
		"})();";

	v8Handler = CefRefPtr<WswCefV8Handler>( new WswCefV8Handler( this ) );
	if( !CefRegisterExtension( "v8/gameUi", code, v8Handler ) ) {
		// TODO: We do not have a browser instance at this moment
	}
}

bool WswCefRenderProcessHandler::OnProcessMessageReceived( CefRefPtr<CefBrowser> browser,
														   CefProcessId source_process,
														   CefRefPtr<CefProcessMessage> message ) {
	if( v8Handler->TryHandle( browser, message ) ) {
		return true;
	}

	Logger()->Warning( "Unexpected message name `%s`", message->GetName().ToString().c_str() );
	return false;
}

// Extracted to reduce template object code duplication
static bool SetKeysAsArgs( const CefV8ValueList &jsArgs, CefRefPtr<CefListValue> messageArgs, CefString &exception ) {
	if( jsArgs.size() < 2 ) {
		return true;
	}

	auto keysArray( jsArgs.front() );

	size_t argNum = 1;
	for( int i = 0, length = keysArray->GetArrayLength(); i < length; ++i ) {
		auto elemValue( keysArray->GetValue( i ) );
		if( !elemValue->IsInt() ) {
			std::stringstream ss;
			ss << "An array element at index " << i << " is not an integer";
			exception = ss.str();
			return false;
		}
		messageArgs->SetInt( argNum++, elemValue->GetIntValue() );
	}

	return true;
}

// Unfortunately we have to define it here and not in syscalls/SyscallsForKeys.cpp
template <typename Request>
void RequestForKeysLauncher<Request>::StartExec( const CefV8ValueList &jsArgs,
												 CefRefPtr<CefV8Value> &retVal,
												 CefString &exception ) {
	if( jsArgs.size() != 1 && jsArgs.size() != 2 ) {
		exception = "Illegal arguments list size, 1 or 2 arguments are expected";
		return;
	}

	if( jsArgs.size() == 2 ) {
		if( !jsArgs[0]->IsArray() ) {
			exception = "An array is expected as a first argument in this case\n";
			return;
		}
	}

	if( !PendingRequestLauncher::ValidateCallback( jsArgs.back(), exception ) ) {
		return;
	}

	auto context( CefV8Context::GetCurrentContext() );
	auto request( TypedPendingRequestLauncher<Request>::NewRequest( context, jsArgs.back() ) );
	auto message( PendingRequestLauncher::NewMessage() );
	auto messageArgs( message->GetArgumentList() );
	messageArgs->SetInt( 0, request->Id() );
	if( !SetKeysAsArgs( jsArgs, messageArgs, exception ) ) {
		return;
	}

	PendingRequestLauncher::Commit( std::move( request ), context, message, retVal, exception );
}