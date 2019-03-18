/** @file
 *
 * @par History
 */

#ifdef HAVE_NODEJS

#include "../bscript/eprog.h"
#include "../clib/esignal.h"
#include "../clib/threadhelp.h"
#include "../polclock.h"
#include "node.h"
#include "nodethread.h"
#include <future>


using namespace Napi;

namespace Pol
{
namespace Node
{
ThreadSafeFunction tsfn;
Napi::ObjectReference requireRef;
std::promise<bool> ready;
std::atomic<bool> running = false;

#ifdef HAVE_NODEJS
void node_thread()
{
  POLLOG_INFO << "Starting node thread";
  char *argv[] = {"node", "./main.js"}, argc = 2;
  RegisterBuiltinModules();
  int ret;
  try
  {
    running = true;
    ret = node::Start( argc, argv );
    POLLOG_INFO << "Node thread finished with val " << ret << "\n";
  }
  catch ( std::exception& ex )
  {
    POLLOG_INFO << "Node threa errored with message " << ex.what() << "\n";
  }
  running = false;
}
#endif


std::future<bool> release( Napi::ObjectReference* ref )
{
  std::shared_ptr<std::promise<bool>> promise = std::make_shared<std::promise<bool>>();


  auto callback = [ref, promise]( Env env, Function jsCallback ) {
    POLLOG_INFO << "Call to release\n";
    try
    {
      ref->Unref();
      ( promise )->set_value( true );
      jsCallback.Call( {Number::New( env, clock() )} );
    }
    catch ( std::exception& ex )
    {
      POLLOG_ERROR << "Exception when attempting to unref obj\n";
      promise->set_exception( std::make_exception_ptr( ex ) );
    }

  };

  ThreadSafeFunction::Status status = tsfn.BlockingCall( callback );
  switch ( status )
  {
  case ThreadSafeFunction::FULL:
    Error::Fatal( "DataSourceThread", "ThreadSafeFunction.*Call() queue is full" );


  case ThreadSafeFunction::OK:
    POLLOG_INFO << "made blocking call\n";
    break;

  case ThreadSafeFunction::CLOSE:
    POLLOG_ERROR << "Attempt to call node when thread is closed\n";
    ( promise )->set_exception( std::make_exception_ptr(
        std::runtime_error( "Attempt to call node when thread is closed" ) ) );
    break;
  default:
    ( promise )->set_exception(
        std::make_exception_ptr( std::runtime_error( "Attempt to call node failed" ) ) );
    // Error::Fatal( "NodeThread", "ThreadSafeFunction.*Call() failed" );
  }
  return promise->get_future();
}

std::future<bool> call( Napi::ObjectReference& ref )
{
  std::shared_ptr<std::promise<bool>> promise = std::make_shared<std::promise<bool>>();


  auto callback = [promise, &ref]( Env env, Function jsCallback ) {
    ref.Get( "default" ).As<Function>().Call( {} );
    ( promise )->set_value( true );
    jsCallback.Call( {String::New( env, "release obj" )} );
  };

  ThreadSafeFunction::Status status = tsfn.BlockingCall( callback );
  switch ( status )
  {
  case ThreadSafeFunction::FULL:
    Error::Fatal( "DataSourceThread", "ThreadSafeFunction.*Call() queue is full" );


  case ThreadSafeFunction::OK:
    POLLOG_INFO << "made blocking call\n";
    break;

  case ThreadSafeFunction::CLOSE:
    POLLOG_ERROR << "Attempt to call node when thread is closed\n";
    ( promise )->set_exception( std::make_exception_ptr(
        std::runtime_error( "Attempt to call node when thread is closed" ) ) );
    break;
  default:
    ( promise )->set_exception(
        std::make_exception_ptr( std::runtime_error( "Attempt to call node failed" ) ) );
    // Error::Fatal( "NodeThread", "ThreadSafeFunction.*Call() failed" );
  }
  return promise->get_future();
}

std::future<Napi::ObjectReference> require( const std::string& name )
{
  std::shared_ptr<std::promise<Napi::ObjectReference>> promise =
      std::make_shared<std::promise<Napi::ObjectReference>>();


  auto callback = [name, promise]( Env env, Function jsCallback ) {
    POLLOG_INFO << "Call to require " << name << "\n";
    auto ret = requireRef.Value().As<Function>().Call( {Napi::String::New( env, name )} );
    auto funct = ret.As<Object>().Get( "default" );
    auto ret2 = funct.As<Object>()
                    .Get( "toString" )
                    .As<Function>()
                    .Call( funct, {} )
                    .As<String>()
                    .Utf8Value();

    POLLOG_INFO << "We got script for " << name << " = " << ret2 << "\n";
    ( promise )->set_value( Napi::ObjectReference::New( ret.As<Object>(), 1 ) );
    jsCallback.Call( {Number::New( env, clock() )} );
  };

  ThreadSafeFunction::Status status = tsfn.BlockingCall( callback );
  switch ( status )
  {
  case ThreadSafeFunction::FULL:
    Error::Fatal( "DataSourceThread", "ThreadSafeFunction.*Call() queue is full" );


  case ThreadSafeFunction::OK:
    POLLOG_INFO << "made blocking call\n";
    break;

  case ThreadSafeFunction::CLOSE:
    POLLOG_ERROR << "Attempt to call node when thread is closed\n";
    ( promise )->set_exception( std::make_exception_ptr(
        std::runtime_error( "Attempt to call node when thread is closed" ) ) );
    break;
  default:
    ( promise )->set_exception(
        std::make_exception_ptr( std::runtime_error( "Attempt to call node failed" ) ) );
    // Error::Fatal( "NodeThread", "ThreadSafeFunction.*Call() failed" );
  }
  return promise->get_future();
}

Napi::ObjectReference Node::obj;

template <typename Callback>
void run_in_node( Callback callback )
{
}

void node_shutdown_thread()
{
  int i = 0;
  while ( !Clib::exit_signalled )
  {
    Core::pol_sleep_ms( 500 );
    i++;
    // if ( i % 2 && i < 10 )
    //{
    //  POLLOG_INFO << "making blocking call\n";
    //}

    if ( false && i % 5 == 0 )
    {
      POLLOG_INFO << "Trying require...\n";
      auto fut = require( "./script.js" );
      fut.wait();
      Node::obj = fut.get();

      POLLOG_INFO << "Got value!\n";
      auto val = call( obj );
      val.wait();
      bool retval = val.get();
      POLLOG_INFO << "Got return " << retval << "\n";


      release( &Node::obj ).wait();
    }

    /*else if ( i >= 10 )
    {
      Clib::exit_signalled = true;
    }*/

    if ( i >= 1 )
    {
      Clib::exit_signalled = true;
    }
  }


  // nodeFuncs.tsfn.Release();
}

std::future<bool> start_node()
{
#ifdef HAVE_NODEJS
  threadhelp::start_thread( node_thread, "Node Thread" );

  threadhelp::start_thread( node_shutdown_thread, "Node Shutdown Listener" );
  //  POLLOG_INFO << "Node thread finished";
#else
  POLLOG_INFO << "Nodejs not compiled";
  ready.set_value( false );

#endif
  return ready.get_future();
}

JavascriptProgram::JavascriptProgram() : Program() {}

Bscript::Program* JavascriptProgram::create()
{
  return new JavascriptProgram;
}

bool JavascriptProgram::hasProgram() const
{
  return false;
}

int JavascriptProgram::read( const char* fname )
{
  try
  {
    auto fut = Node::require( std::string( "./" ) + fname );
    fut.wait();
    obj = fut.get();
    POLLOG_INFO << "Got a successful read for " << fname << "\n";
    return 0;
  }
  catch ( std::exception& ex )
  {
    ERROR_PRINT << "Exception caught while loading node script " << fname << ": " << ex.what()
                << "\n";
  }
  return -1;
}

void JavascriptProgram::package( const Plib::Package* package )
{
  pkg = package;
}

std::string JavascriptProgram::scriptname() const
{
  return name;
}

Bscript::Program::ProgramType JavascriptProgram::type() const
{
  return Bscript::Program::JAVASCRIPT;
}

JavascriptProgram::~JavascriptProgram()
{
  // Node::release( &obj );
}

static Napi::Value CreateTSFN( CallbackInfo& info )
{
  Napi::Env env = info.Env();
  Napi::HandleScope scope( env );

  requireRef = Napi::Persistent( info[1].As<Object>() );

  tsfn = ThreadSafeFunction::New( env,

                                  info[0].As<Function>(),

                                  Object(), "work_name", 0, 1,
                                  (void*)nullptr,                    // data for finalize cb
                                  []( Napi::Env, void*, void* ) {},  // finalize cb
                                  (void*)nullptr );


  POLLOG_INFO << "setting..\n";
  ready.set_value( true );
  POLLOG_INFO << "set promise value!\n";

  auto callback = []( Napi::Env env, Napi::Function jsCallback ) {
    POLLOG_INFO << "callback from blocking call called!\n";
    jsCallback.Call( {Number::New( env, clock() )} );
  };

  ThreadSafeFunction::Status status = tsfn.BlockingCall( callback );
  switch ( status )
  {
  case ThreadSafeFunction::FULL:
    Error::Fatal( "DataSourceThread", "ThreadSafeFunction.*Call() queue is full" );


  case ThreadSafeFunction::OK:
    break;

  case ThreadSafeFunction::CLOSE:
    Error::Fatal( "DataSourceThread", "ThreadSafeFunction.*Call() is closed" );

  default:
    Error::Fatal( "DataSourceThread", "ThreadSafeFunction.*Call() failed" );
  }

  POLLOG_INFO << "made first blocking call\n";
  return Boolean::New( env, true );
}


static Napi::Object InitializeNAPI( Napi::Env env, Napi::Object exports )
{
  POLLOG_INFO << "initializing";
  exports.Set( "start", Function::New( env, CreateTSFN ) );
  POLLOG_INFO << "inited";
  return exports;
}


NODE_API_MODULE_LINKED( tsfn, InitializeNAPI )

void RegisterBuiltinModules()
{
  _register_tsfn();
}

void cleanup()
{
  release( &Node::requireRef ).wait();

  if ( !tsfn.Release() )
  {
    Error::Fatal( "SecondaryThread", "ThreadSafeFunction.Release() failed" );
  }
  else
  {
    POLLOG_INFO << "released\n";
  }
}


}  // namespace Node
}  // namespace Pol

#endif
