#ifndef SRC_POLYFILL_H
#define SRC_POLYFILL_H

#include <nan.h>

// what doesn't exist in nan exists here

#if (NODE_MODULE_VERSION > 0x000B)

#define EXTERNAL_NEW(x) External::New(Isolate::GetCurrent(), x)
#define UV_ASYNC_CB(x) void x(uv_async_t *handle)

#else

#define EXTERNAL_NEW(x) External::New(x)
#define UV_ASYNC_CB(x) void x(uv_async_t *handle, int status)

#endif

// Abstraction of the non-deferred v8::Function::Call parameter changes
// This is expected to land in Nan 2.2.1 see: https://github.com/nodejs/nan/pull/496
#if defined(V8_MAJOR_VERSION) && (V8_MAJOR_VERSION > 4 ||                      \
  (V8_MAJOR_VERSION == 4 && defined(V8_MINOR_VERSION) && V8_MINOR_VERSION >= 3))

	NAN_INLINE v8::MaybeLocal<v8::Value> Call(
		v8::Local<v8::Function> fun
	  , v8::Local<v8::Object> recv
	  , int argc
	  , v8::Local<v8::Value> argv[]) {
	  return fun->Call(Nan::GetCurrentContext(), recv, argc, argv);
	}

#else

	NAN_INLINE Nan::MaybeLocal<v8::Value> Call(
		v8::Local<v8::Function> fun
	  , v8::Local<v8::Object> recv
	  , int argc
	  , v8::Local<v8::Value> argv[]) {
	  return Nan::MaybeLocal<v8::Value>(fun->Call(recv, argc, argv));
	}

#endif

#endif
