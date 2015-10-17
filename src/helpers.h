#ifndef SRC_HELPERS_H
#define SRC_HELPERS_H

#include <vector>
#include <v8.h>
#include <nan.h>
#include "polyfill.h"

using namespace v8;

#define V8STR(str) Nan::New<String>(str).ToLocalChecked()
#define V8SYM(str) Nan::New<String>(str).ToLocalChecked()

const PropertyAttribute CONST_PROP = static_cast<PropertyAttribute>(ReadOnly|DontDelete);

inline static void setConst(Local<Object> obj, const char* const name, Local<Value> value){
	obj->ForceSet(Nan::New(name).ToLocalChecked(), value, CONST_PROP);
}

#endif
