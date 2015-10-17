#include "node_usb.h"
#include <string.h>

#define MAX_PORTS 7

Local<Object> makeBuffer(const unsigned char* ptr, unsigned length) {
	return Nan::NewBuffer((char*) ptr, (uint32_t) length).ToLocalChecked();
}

// static
Nan::Persistent<Function> Device::constructor;

Device::Device(libusb_device* d): device(d), device_handle(0) {
	libusb_ref_device(device);
	DEBUG_LOG("Created device %p", this);
}

Device::~Device(){
	//TODO: Research destruction/closure
	/*
	https://code.google.com/p/v8-juice/wiki/CommentaryOnV8#No_GC_guaranty

	DEBUG_LOG("Freed device %p", this);
	libusb_close(device_handle);
	libusb_unref_device(device);
	*/
}

// static
NAN_MODULE_INIT(Device::Init) {
	Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
	tpl->SetClassName(Nan::New("Device").ToLocalChecked());
	tpl->InstanceTemplate()->SetInternalFieldCount(1);

	// Setup the Device object prototype methods
	Nan::SetPrototypeMethod(tpl, "__getConfigDescriptor", GetConfigDescriptor);
	Nan::SetPrototypeMethod(tpl, "__open", Open);
	Nan::SetPrototypeMethod(tpl, "__close", Close);
	Nan::SetPrototypeMethod(tpl, "reset", Reset);

	Nan::SetPrototypeMethod(tpl, "__claimInterface", ClaimInterface);
	Nan::SetPrototypeMethod(tpl, "__releaseInterface", ReleaseInterface);
	Nan::SetPrototypeMethod(tpl, "__setInterface", SetInterface);

	Nan::SetPrototypeMethod(tpl, "__isKernelDriverActive", IsKernelDriverActive);
	Nan::SetPrototypeMethod(tpl, "__detachKernelDriver", DetachKernelDriver);
	Nan::SetPrototypeMethod(tpl, "__attachKernelDriver", AttachKernelDriver);

	// Stick a pin in the js constructor function
	auto confun = Nan::GetFunction(tpl).ToLocalChecked();
	constructor.Reset(confun);

	// Add the Device constructor to the module
	target->Set(Nan::New("Device").ToLocalChecked(), confun);
}

// static
Local<Object> Device::FromLibUSBDevice(libusb_device* dev) {
	Nan::EscapableHandleScope scope;

	// Box the libusb_device pointer
	Local<Value> argv[1] = { Nan::New<External>(dev) };
	// New up a js instance of Device
	Local<Object> device = Nan::New(constructor)->NewInstance(1, argv);

	return scope.Escape(device);
}

// static
void Device::New(const Nan::FunctionCallbackInfo<Value>& args) {
	if (!args.IsConstructCall()) return Nan::ThrowError("Must be called with `new`!");

	if(args.Length() == 0 || !args[0]->IsExternal())
		return Nan::ThrowError("Must provide a pointer to the libusb_device struct");
	
	libusb_device* pdev = static_cast<libusb_device*>(External::Cast(*args[0])->Value());
	Device* device = new Device(pdev);

	Local<Object> self = args.This();

	// Set the dev properties
	setConst(self, "busNumber", Nan::New<Uint32>(libusb_get_bus_number(device->device)));
	setConst(self, "deviceAddress", Nan::New<Uint32>(libusb_get_device_address(device->device)));

	// Make an empty js obj to hold the descriptor values
	Local<Object> jsdd = Nan::New<Object>();
	setConst(self, "deviceDescriptor", jsdd);

	// Get the libusb descriptor obj
	struct libusb_device_descriptor dd;
	CHECK_USB(libusb_get_device_descriptor(device->device, &dd));

	// Transfer the descriptor values from ratty C++ to happy jsland
	setConst(jsdd, "bLength", Nan::New<Uint32>(dd.bLength));
	setConst(jsdd, "bDescriptorType", Nan::New<Uint32>(dd.bDescriptorType));
	setConst(jsdd, "bcdUSB", Nan::New<Uint32>(dd.bcdUSB));
	setConst(jsdd, "bDeviceClass", Nan::New<Uint32>(dd.bDeviceClass));
	setConst(jsdd, "bDeviceSubClass", Nan::New<Uint32>(dd.bDeviceSubClass));
	setConst(jsdd, "bDeviceProtocol", Nan::New<Uint32>(dd.bDeviceProtocol));
	setConst(jsdd, "bMaxPacketSize0", Nan::New<Uint32>(dd.bMaxPacketSize0));
	setConst(jsdd, "idVendor", Nan::New<Uint32>(dd.idVendor));
	setConst(jsdd, "idProduct", Nan::New<Uint32>(dd.idProduct));
	setConst(jsdd, "bcdDevice", Nan::New<Uint32>(dd.bcdDevice));
	setConst(jsdd, "iManufacturer", Nan::New<Uint32>(dd.iManufacturer));
	setConst(jsdd, "iProduct", Nan::New<Uint32>(dd.iProduct));
	setConst(jsdd, "iSerialNumber", Nan::New<Uint32>(dd.iSerialNumber));
	setConst(jsdd, "bNumConfigurations", Nan::New<Uint32>(dd.bNumConfigurations));

	// Get the device port numbers
	uint8_t port_numbers[MAX_PORTS];
	int ret = libusb_get_port_numbers(device->device, port_numbers, MAX_PORTS);
	CHECK_USB(ret);
	
	Local<Array> array = Nan::New<Array>(ret);
	setConst(self, "portNumbers", array);
	for (int i = 0; i < ret; ++ i) {
		array->Set(i, Nan::New(port_numbers[i]));
	}

	// return this;
	args.GetReturnValue().Set(args.This());
}

// static
void Device::Open(const Nan::FunctionCallbackInfo<Value>& args) {
	Device* self = Device::Unwrap(args.Holder());

	if (!self->device_handle){
		CHECK_USB(libusb_open(self->device, &self->device_handle));
	}
}

// static
void Device::Close(const Nan::FunctionCallbackInfo<Value>& args) {
	Device* self = Device::Unwrap(args.Holder());

	if (self->canClose()) {
		libusb_close(self->device_handle);
		self->device_handle = NULL;
	} else {
		return Nan::ThrowError("Can't close device with a pending request");
	}
}

// static
void Device::GetConfigDescriptor(const Nan::FunctionCallbackInfo<Value>& args) {
	Device* self = Device::Unwrap(args.Holder());

	libusb_config_descriptor* cdesc;
	CHECK_USB(libusb_get_active_config_descriptor(self->device, &cdesc));

	Local<Object> v8cdesc = Nan::New<Object>();

	setConst(v8cdesc, "bLength", Nan::New<Uint32>(cdesc->bLength));
	setConst(v8cdesc, "bDescriptorType", Nan::New<Uint32>(cdesc->bDescriptorType));
	setConst(v8cdesc, "wTotalLength", Nan::New<Uint32>(cdesc->wTotalLength));
	setConst(v8cdesc, "bNumInterfaces", Nan::New<Uint32>(cdesc->bNumInterfaces));
	setConst(v8cdesc, "bConfigurationValue", Nan::New<Uint32>(cdesc->bConfigurationValue));
	setConst(v8cdesc, "bmAttributes", Nan::New<Uint32>(cdesc->bmAttributes));
	
	// Libusb 1.0 typo'd bMaxPower as MaxPower
	setConst(v8cdesc, "bMaxPower", Nan::New<Uint32>(cdesc->MaxPower));
	setConst(v8cdesc, "extra", makeBuffer(cdesc->extra, cdesc->extra_length));

	Local<Array> v8interfaces = Nan::New<Array>(cdesc->bNumInterfaces);
	v8cdesc->ForceSet(V8SYM("interfaces"), v8interfaces);

	for (int idxInterface = 0; idxInterface < cdesc->bNumInterfaces; idxInterface++) {
		int numAltSettings = cdesc->interface[idxInterface].num_altsetting;

		Local<Array> v8altsettings = Nan::New<Array>(numAltSettings);
		v8interfaces->Set(idxInterface, v8altsettings);

		for (int idxAltSetting = 0; idxAltSetting < numAltSettings; idxAltSetting++) {
			const libusb_interface_descriptor& idesc =
				cdesc->interface[idxInterface].altsetting[idxAltSetting];

			Local<Object> v8idesc = Nan::New<Object>();
			v8altsettings->Set(idxAltSetting, v8idesc);

			setConst(v8idesc, "bLength", Nan::New<Uint32>(idesc.bLength));
			setConst(v8idesc, "bDescriptorType", Nan::New<Uint32>(idesc.bDescriptorType));
			setConst(v8idesc, "bInterfaceNumber", Nan::New<Uint32>(idesc.bInterfaceNumber));
			setConst(v8idesc, "bAlternateSetting", Nan::New<Uint32>(idesc.bAlternateSetting));
			setConst(v8idesc, "bNumEndpoints", Nan::New<Uint32>(idesc.bNumEndpoints));
			setConst(v8idesc, "bInterfaceClass", Nan::New<Uint32>(idesc.bInterfaceClass));
			setConst(v8idesc, "bInterfaceSubClass", Nan::New<Uint32>(idesc.bInterfaceSubClass));
			setConst(v8idesc, "bInterfaceProtocol", Nan::New<Uint32>(idesc.bInterfaceProtocol));
			setConst(v8idesc, "iInterface", Nan::New<Uint32>(idesc.iInterface));

			setConst(v8idesc, "extra", makeBuffer(idesc.extra, idesc.extra_length));

			Local<Array> v8endpoints = Nan::New<Array>(idesc.bNumEndpoints);
			setConst(v8idesc, "endpoints", v8endpoints);
			for (int idxEndpoint = 0; idxEndpoint < idesc.bNumEndpoints; idxEndpoint++){
				const libusb_endpoint_descriptor& edesc = idesc.endpoint[idxEndpoint];

				Local<Object> v8edesc = Nan::New<Object>();
				v8endpoints->Set(idxEndpoint, v8edesc);

				setConst(v8edesc, "bLength", Nan::New<Uint32>(edesc.bLength));
				setConst(v8edesc, "bDescriptorType", Nan::New<Uint32>(edesc.bDescriptorType));
				setConst(v8edesc, "bEndpointAddress", Nan::New<Uint32>(edesc.bEndpointAddress));
				setConst(v8edesc, "bmAttributes", Nan::New<Uint32>(edesc.bmAttributes));
				setConst(v8edesc, "wMaxPackeSize", Nan::New<Uint32>(edesc.wMaxPacketSize));
				setConst(v8edesc, "bInterval", Nan::New<Uint32>(edesc.bInterval));
				setConst(v8edesc, "bRefresh", Nan::New<Uint32>(edesc.bRefresh));
				setConst(v8edesc, "bSynchAddress", Nan::New<Uint32>(edesc.bSynchAddress));

				setConst(v8edesc, "extra", makeBuffer(edesc.extra, edesc.extra_length));
			}
		}
	}

	libusb_free_config_descriptor(cdesc);

	args.GetReturnValue().Set(v8cdesc);
}

// static
void Device::IsKernelDriverActive(const Nan::FunctionCallbackInfo<Value>& args) {
	if(args.Length() == 0 || !args[0]->IsNumber())
		return Nan::ThrowTypeError("The interface index to check must be provided");

	Device* self = Device::Unwrap(args.Holder());
	self->RequireOpen();

	int r = libusb_kernel_driver_active(self->device_handle, Nan::To<int>(args[0]).FromJust());
	CHECK_USB(r);

	args.GetReturnValue().Set(Nan::New<Boolean>(r));
}

// static
void Device::DetachKernelDriver(const Nan::FunctionCallbackInfo<Value>& args) {
	if(args.Length() == 0 || !args[0]->IsNumber())
		return Nan::ThrowTypeError("The interface index to to detach for must be provided");

	Device* self = Device::Unwrap(args.Holder());
	self->RequireOpen();

	CHECK_USB(libusb_detach_kernel_driver(self->device_handle, Nan::To<int>(args[0]).FromJust()));
}

// static
void Device::AttachKernelDriver(const Nan::FunctionCallbackInfo<Value>& args) {
	if(args.Length() == 0 || !args[0]->IsNumber())
		return Nan::ThrowTypeError("The interface index to attach for must be provided");

	Device* self = Device::Unwrap(args.Holder());
	self->RequireOpen();

	CHECK_USB(libusb_attach_kernel_driver(self->device_handle, Nan::To<int>(args[0]).FromJust()));
}

// static
void Device::ClaimInterface(const Nan::FunctionCallbackInfo<Value>& args) {
	if(args.Length() == 0 || !args[0]->IsNumber())
		return Nan::ThrowTypeError("The interface index to claim must be provided");

	Device* self = Device::Unwrap(args.Holder());
	self->RequireOpen();

	CHECK_USB(libusb_claim_interface(self->device_handle, Nan::To<int>(args[0]).FromJust()));
}

// static
void Device::SetInterface(const Nan::FunctionCallbackInfo<Value>& args) {
	if(args.Length() == 0 || !args[0]->IsNumber())
		return Nan::ThrowTypeError("The interface index to set must be specified");
	if(args.Length() == 1 || !args[0]->IsNumber())
		return Nan::ThrowTypeError("The alternate setting index must be specified");
	if(args.Length() == 2 || !args[0]->IsFunction())
		return Nan::ThrowTypeError("A completion callback must be provided");

	Device* self = Device::Unwrap(args.Holder());
	self->RequireOpen();

	//TODO: Initiate request and handle completion
}

// static
void Device::ReleaseInterface(const Nan::FunctionCallbackInfo<Value>& args) {
	if(args.Length() == 0 || !args[0]->IsNumber())
		return Nan::ThrowTypeError("The interface index to release must be specified");
	if(args.Length() == 1 || !args[0]->IsFunction())
		return Nan::ThrowTypeError("A completion callback must be provided");

	Device* self = Device::Unwrap(args.Holder());
	self->RequireOpen();

	//TODO: Initiate request and handle completion
}

// static
void Device::Reset(const Nan::FunctionCallbackInfo<Value>& args) {
	if(args.Length() == 0 || !args[0]->IsFunction())
		return Nan::ThrowTypeError("A callback must be provided");

	Device* self = Device::Unwrap(args.Holder());
	self->RequireOpen();

	//TODO: Initiate request and handle completion
}
//////******* NNNNEWWWWW ENENNENENEND ///////


/* OLD LIFETIME MANAGMENT
NAN_WEAK_CALLBACK(DeviceWeakCallback) {
	Device::unpin(data.GetParameter());
}

// Get a V8 instance for a libusb_device: either the existing one from the map,
// or create a new one and add it to the map.
Local<Value> Device::get(libusb_device* dev) {
	auto it = byPtr.find(dev);
	if (it != byPtr.end()) {
		return Nan::New(it->second->persistent);
	} else {
		Local<FunctionTemplate> constructorHandle = Nan::New<FunctionTemplate>(device_constructor);
		Local<Value> argv[1] = { EXTERNAL_NEW(new Device(dev)) };
		Handle<Value> v = constructorHandle->GetFunction()->NewInstance(1, argv);
		auto p = Nan::MakeWeakPersistent(v, dev, DeviceWeakCallback);
		byPtr.insert(std::make_pair(dev, p));
		return v;
	}
}

void Device::unpin(libusb_device* device) {
	byPtr.erase(device);
	DEBUG_LOG("Removed cached device %p", device);
}
*/
class UsbRequest {
  public:
	void submit(Device* d, Handle<Function> cb, uv_work_cb backend, uv_work_cb after) {
		callback.Reset(cb);
		device = d;
		//TMP device->ref();
		req.data = this;
		uv_queue_work(uv_default_loop(), &req, backend, (uv_after_work_cb) after);
	}

	static void default_after(uv_work_t *req) {
		Nan::HandleScope();
		auto baton = (UsbRequest*) req->data;

		auto device = baton->device->handle();
		//TMP baton->device->unref();

		if (!Nan::New(baton->callback).IsEmpty()) {
			Local<Value> error = Nan::Undefined();
			if (baton->errcode < 0){
				error = libusbException(baton->errcode);
			}
			Local<Value> argv[1] = {error};
			Nan::TryCatch try_catch;
			Nan::MakeCallback(device, Nan::New(baton->callback), 1, argv);
			if (try_catch.HasCaught()) {
				Nan::FatalException(try_catch);
			}
			baton->callback.Reset();
		}
		delete baton;
	}

  protected:
	uv_work_t req;
	Device* device;
	Nan::Persistent<Function> callback;
	int errcode;
};

class ResetRequest : public UsbRequest {
	void begin(Device* device, Local<Function> callback) {
		auto baton = new ResetRequest();
		baton->submit(device, callback, &backend, &default_after);
	}

	static void backend(uv_work_t *req) {
		auto baton = (ResetRequest*) req->data;
		//baton->errcode = libusb_reset_device(baton->device->device_handle);
	}
};

class ReleaseInterfaceRequest : public UsbRequest {
	int interface;

	void begin(Device* device, int interface, Local<Function> callback) {
		auto baton = new ReleaseInterfaceRequest();
		baton->interface = interface;
		baton->submit(device, callback, &backend, &default_after);
	}

	static void backend(uv_work_t *req) {
		auto baton = (ReleaseInterfaceRequest*) req->data;
		//baton->errcode = libusb_release_interface(baton->device->device_handle, baton->interface);
	}
};

class SetInterfaceRequest : public UsbRequest {
	int interface;
	int altsetting;

	void begin(Device* device, int interface, int altsetting, Local<Function> callback) {
		auto baton = new SetInterfaceRequest();
		baton->interface = interface;
		baton->altsetting = altsetting;
		baton->submit(device, callback, &backend, &default_after);
	}

	static void backend(uv_work_t *req) {
		auto baton = (SetInterfaceRequest*) req->data;
		/*
		baton->errcode = libusb_set_interface_alt_setting(
			baton->device->device_handle, baton->interface, baton->altsetting);
			*/
	}
};

// @joshperry ✈
