#include "rtObjectWrapper.h"
#include "rtFunctionWrapper.h"
#include "rtWrapperUtils.h"

#include <rtLog.h>

using namespace v8;

static const char* kClassName = "Object";
static const char* kFuncAllKeys = "allKeys";
static const char* kPropLength = "length";

static Persistent<Function> ctor;

rtObjectWrapper::rtObjectWrapper(const rtObjectRef& ref)
  : rtWrapper(ref)
{

}

rtObjectWrapper::~rtObjectWrapper()
{

}

void rtObjectWrapper::exportPrototype(Handle<Object> exports)
{
  Local<FunctionTemplate> tmpl = FunctionTemplate::New(create);
  tmpl->SetClassName(String::NewSymbol(kClassName));

  Local<ObjectTemplate> inst = tmpl->InstanceTemplate();
  inst->SetInternalFieldCount(1);
  inst->SetNamedPropertyHandler(
    &getPropertyByName,
    &setPropertyByName,
    NULL,
    NULL,
    &getEnumerablePropertyNames);

  inst->SetIndexedPropertyHandler(
    &getPropertyByIndex,
    &setPropertyByIndex,
    NULL,
    NULL,
    &getEnumerablePropertyIndecies);

  ctor = Persistent<Function>::New(tmpl->GetFunction());
  exports->Set(String::NewSymbol(kClassName), ctor);
}

Handle<Object> rtObjectWrapper::createFromObjectReference(const rtObjectRef& ref)
{
  HandleScope scope;
  Local<Value> argv[1] = { External::New(ref.getPtr()) };
  Local<Object> obj = ctor->NewInstance(1, argv);
  return scope.Close(obj);
}

rtValue rtObjectWrapper::unwrapObject(const Local<Object>& obj)
{
  return rtValue(unwrap(obj));
}

Handle<Array> rtObjectWrapper::getEnumerablePropertyNames(const AccessorInfo& info)
{
  rtObjectWrapper* wrapper = node::ObjectWrap::Unwrap<rtObjectWrapper>(info.This());
  if (!wrapper)
    return Handle<Array>();

  rtObjectRef ref = wrapper->mWrappedObject;
  if (!ref)
    return Handle<Array>();

  rtObjectRef keys = ref.get<rtObjectRef>(kFuncAllKeys);
  if (!keys)
    return Handle<Array>();

  uint32_t length = keys.get<uint32_t>(kPropLength);
  Local<Array> props = Array::New(length);

  for (uint32_t i = 0; i < length; ++i)
    props->Set(Number::New(i), String::New(keys.get<rtString>(i).cString()));

  return props;
}


Handle<Array> rtObjectWrapper::getEnumerablePropertyIndecies(const AccessorInfo& info)
{
  rtObjectWrapper* wrapper = node::ObjectWrap::Unwrap<rtObjectWrapper>(info.This());
  if (!wrapper)
    return Handle<Array>();

  rtObjectRef ref = wrapper->mWrappedObject;
  if (!ref)
    return Handle<Array>();

  rtObjectRef keys = ref.get<rtObjectRef>(kFuncAllKeys);
  if (!keys)
    return Handle<Array>();

  uint32_t length = keys.get<uint32_t>(kPropLength);
  Local<Array> props = Array::New(length);

  for (uint32_t i = 0; i < length; ++i)
    props->Set(Number::New(i), Number::New(i));

  return props;
}

Handle<Value> rtObjectWrapper::getPropertyByName(Local<String> prop, const AccessorInfo& info)
{
  rtString name = toString(prop);

  rtObjectWrapper* wrapper = node::ObjectWrap::Unwrap<rtObjectWrapper>(info.This());
  if (!wrapper)
    return Handle<Value>(Undefined());

  rtObjectRef ref = wrapper->mWrappedObject;
  if (!ref)
    return Handle<Value>(Undefined());

  rtValue value;
  rtWrapperSceneUpdateEnter();
  rtError err = ref->Get(name.cString(), &value);
  rtWrapperSceneUpdateExit();

  if (err != RT_OK)
  {
    if (err == RT_PROP_NOT_FOUND)
      return Handle<Value>(Undefined());
    else
      return ThrowException(Exception::Error(String::New(rtStrError(err))));
  }

  return rt2js(value);
}


Handle<Value> rtObjectWrapper::getPropertyByIndex(uint32_t index, const AccessorInfo& info)
{
  rtObjectWrapper* wrapper = node::ObjectWrap::Unwrap<rtObjectWrapper>(info.This());
  if (!wrapper)
    return Handle<Value>(Undefined());

  rtObjectRef ref = wrapper->mWrappedObject;
  if (!ref)
    return Handle<Value>(Undefined());

  rtValue value;
  rtWrapperSceneUpdateEnter();
  rtError err = ref->Get(index, &value);
  rtWrapperSceneUpdateExit();

  if (err != RT_OK)
  {
    if (err == RT_PROP_NOT_FOUND)
      return Handle<Value>(Undefined());
    else
      return ThrowException(Exception::Error(String::New(rtStrError(err))));
  }

  return rt2js(value);

}

Handle<Value> rtObjectWrapper::setPropertyByName(Local<String> prop, Local<Value> val, const AccessorInfo& info)
{
  rtString name = toString(prop);

  rtWrapperError error;
  rtValue value = js2rt(val, &error);
  if (error.hasError())
    return ThrowException(error.toTypeError());

  rtWrapperSceneUpdateEnter();
  rtError err = unwrap(info)->Set(name.cString(), &value);
  rtWrapperSceneUpdateExit();
  return err == RT_OK
    ? val
    : Handle<Value>();
}

Handle<Value> rtObjectWrapper::setPropertyByIndex(uint32_t index, Local<Value> val, const AccessorInfo& info)
{
  rtWrapperError error;
  rtValue value = js2rt(val, &error);
  if (error.hasError())
    return ThrowException(error.toTypeError());

  rtWrapperSceneUpdateEnter();
  rtError err = unwrap(info)->Set(index, &value);
  rtWrapperSceneUpdateExit();
  return err == RT_OK
    ? val
    : Handle<Value>();
}

Handle<Value> rtObjectWrapper::create(const Arguments& args)
{ 
  if (args.IsConstructCall())
  {
    rtObject* obj = reinterpret_cast<rtObject*>(Local<External>::Cast(args[0])->Value());
    rtObjectWrapper* wrapper = new rtObjectWrapper(obj);
    wrapper->Wrap(args.This());
    return args.This();
  }
  else
  {
    // invoked as rtObjectWrapper()
    const int argc = 1;

    HandleScope scope;
    Local<Value> argv[argc] = { args[0] };
    return scope.Close(ctor->NewInstance(argc, argv));
  }
}

jsObjectWrapper::jsObjectWrapper(const Handle<Value>& obj)
  : mRefCount(0)
{
  assert(obj->IsObject());
  mObject = Persistent<Object>::New(Handle<Object>::Cast(obj));
}

jsObjectWrapper::~jsObjectWrapper()
{
  mObject.Dispose();
}

unsigned long jsObjectWrapper::AddRef()
{
  return rtAtomicInc(&mRefCount);
}

unsigned long jsObjectWrapper::Release()
{
  unsigned long l = rtAtomicDec(&mRefCount);
  if (l == 0) delete this;
  return l;
}

rtError jsObjectWrapper::getAllKeys(rtValue* value)
{
  Local<Array> names = mObject->GetPropertyNames();

  rtRefT<rtArrayObject> result(new rtArrayObject);
  for (int i = 0, n = names->Length(); i < n; ++i)
  {
    rtWrapperError error;
    rtValue val = js2rt(names->Get(i), &error);
    if (error.hasError())
      return RT_FAIL;
    else
      result->pushBack(val);
  }

  *value = rtValue(result);
  return RT_OK;
}

rtError jsObjectWrapper::Get(const char* name, rtValue* value)
{
  if (!value)
    return RT_ERROR_INVALID_ARG;
  if (!name)
    return RT_ERROR_INVALID_ARG;

  if (strcmp(name, kFuncAllKeys) == 0)
    return getAllKeys(value);

  Local<String> s = String::New(name);
  if (!mObject->Has(s))
    return RT_PROPERTY_NOT_FOUND;

  rtWrapperError error;
  *value = js2rt(mObject->Get(s), &error);
  if (error.hasError())
    return RT_ERROR_INVALID_ARG;

  return RT_OK;
}

rtError jsObjectWrapper::Get(uint32_t i, rtValue* value)
{
  if (!value)
    return RT_ERROR_INVALID_ARG;

  if (!mObject->Has(i))
    return RT_PROPERTY_NOT_FOUND;

  rtWrapperError error;
  *value = js2rt(mObject->Get(i), &error);
  if (error.hasError())
    return RT_ERROR_INVALID_ARG;

  return RT_OK;
}

rtError jsObjectWrapper::Set(const char* name, const rtValue* value)
{
  if (!value)
    return RT_ERROR_INVALID_ARG;
  if (!name)
    return RT_ERROR_INVALID_ARG;

  if (!mObject->Set(String::New(name), rt2js(*value)))
    return RT_FAIL;

  return RT_OK;
}

rtError jsObjectWrapper::Set(uint32_t i, const rtValue* value)
{
  if (!value)
    return RT_ERROR_INVALID_ARG;

  if (!mObject->Set(i, rt2js(*value)))
    return RT_FAIL;

  return RT_OK;
}

