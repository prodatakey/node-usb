#ifndef SRC_NODE_USB_H
#define SRC_NODE_USB_H

#include <assert.h>
#include <string>
#include <map>

#ifdef _WIN32
#include <WinSock2.h>
#endif
#include <libusb.h>
#include <v8.h>

#include <node.h>
#include <node_buffer.h>
#include <uv.h>
#ifndef USE_POLL
#include "uv_async_queue.h"
#endif

#include "helpers.h"

using namespace v8;

Local<Value> libusbException(int errorno);

class Device: public Nan::ObjectWrap {
  public:
	static NAN_MODULE_INIT(Init);
	static inline Device* Unwrap(Local<Object> obj) {
		return Nan::ObjectWrap::Unwrap<Device>(obj);
	};
	static Local<Object> FromLibUSBDevice(libusb_device* dev);

	bool IsOpen() const {
		return !!device_handle;
	}

	void RequireOpen() const {
		if(!IsOpen())
			return Nan::ThrowError("The Device must be open");
	}

	libusb_device_handle* DeviceHandle() const {
		return device_handle;
	}

  private:
	static Local<Value> get(libusb_device* handle);

	inline void ref(){Ref();}
	inline void unref(){Unref();}
	inline bool canClose(){return refs_ == 0;}
	inline void attach(Local<Object> o){Wrap(o);}

	static void unpin(libusb_device* device);

	// No, bad, don't touch.
	explicit Device(libusb_device* d);
	~Device(); // https://code.google.com/p/v8-juice/wiki/CommentaryOnV8#No_GC_guaranty

	// JS members
	static Nan::Persistent<Function> constructor;

	static void New(const Nan::FunctionCallbackInfo<Value>& args);

	static void GetConfigDescriptor(const Nan::FunctionCallbackInfo<Value>& args);
	static void Open(const Nan::FunctionCallbackInfo<Value>& args);
	static void Close(const Nan::FunctionCallbackInfo<Value>& args);
	static void Reset(const Nan::FunctionCallbackInfo<Value>& args);

	static void ClaimInterface(const Nan::FunctionCallbackInfo<Value>& args);
	static void SetInterface(const Nan::FunctionCallbackInfo<Value>& args);
	static void ReleaseInterface(const Nan::FunctionCallbackInfo<Value>& args);

	static void IsKernelDriverActive(const Nan::FunctionCallbackInfo<Value>& args);
	static void DetachKernelDriver(const Nan::FunctionCallbackInfo<Value>& args);
	static void AttachKernelDriver(const Nan::FunctionCallbackInfo<Value>& args);

	libusb_device* device;
	libusb_device_handle* device_handle;
};

class Transfer : public Nan::ObjectWrap {
  public:
	static NAN_MODULE_INIT(Init);
	static inline Transfer* Unwrap(Local<Object> obj) {
		return Nan::ObjectWrap::Unwrap<Transfer>(obj);
	};

  private:
	inline void ref(){Ref();}
	inline void unref(){Unref();}
	inline void attach(Local<Object> o){Wrap(o);}

	Transfer();
	~Transfer();

	static LIBUSB_CALL void CompletionCallback(libusb_transfer* transfer);
	static void QueueCompletion(libusb_transfer* transfer);
	void HandleCompletion();

	// JS Members
	static Nan::Persistent<Function> constructor;

	static void New(const Nan::FunctionCallbackInfo<Value>& args);

	static void Submit(const Nan::FunctionCallbackInfo<Value>& args);
	static void Cancel(const Nan::FunctionCallbackInfo<Value>& args);

	libusb_transfer* transfer;
	Device* device;
	Nan::Persistent<Object> v8buffer;
	Nan::Persistent<Function> v8callback;
#ifndef USE_POLL
	static UVQueue<libusb_transfer*> completionQueue;
#endif
};


#define CHECK_USB(r) \
	if (r < LIBUSB_SUCCESS) { \
		return Nan::ThrowError(libusbException(r)); \
	}

#define CALLBACK_ARG(CALLBACK_ARG_IDX) \
	Local<Function> callback; \
	if (args.Length() > (CALLBACK_ARG_IDX)) { \
		if (!args[CALLBACK_ARG_IDX]->IsFunction()) { \
			return Nan::ThrowTypeError("Argument " #CALLBACK_ARG_IDX " must be a function"); \
		} \
		callback = args[CALLBACK_ARG_IDX].As<Function>(); \
	} \

#ifdef DEBUG
  #define DEBUG_HEADER fprintf(stderr, "node-usb [%s:%s() %d]: ", __FILE__, __FUNCTION__, __LINE__);
  #define DEBUG_FOOTER fprintf(stderr, "\n");
  #define DEBUG_LOG(...) DEBUG_HEADER fprintf(stderr, __VA_ARGS__); DEBUG_FOOTER
#else
  #define DEBUG_LOG(...)
#endif

#endif

// @joshperry ✈
