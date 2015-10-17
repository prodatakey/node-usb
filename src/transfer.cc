#include "node_usb.h"

auto UnwrapTransfer = &Nan::ObjectWrap::Unwrap<Transfer>;

// static
Nan::Persistent<Function> Transfer::constructor;

#ifndef USE_POLL
// static
UVQueue<libusb_transfer*> Transfer::completionQueue(CompletionCallback);
#endif

Transfer::Transfer() {
	transfer = libusb_alloc_transfer(0);

	#ifndef USE_POLL
	transfer->callback = Transfer::QueueCompletion;
	#else
	transfer->callback = Transfer::CompletionCallback;
	#endif
	transfer->user_data = this;
	DEBUG_LOG("Created Transfer %p", this);
}

Transfer::~Transfer() {
	DEBUG_LOG("Freed Transfer %p", this);
	v8callback.Reset();
	libusb_free_transfer(transfer);
}

// static
void Transfer::Init(Local<Object> exports) {
	Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
	tpl->SetClassName(Nan::New("Transfer").ToLocalChecked());
	tpl->InstanceTemplate()->SetInternalFieldCount(1);

	Nan::SetPrototypeMethod(tpl, "submit", Submit);
	Nan::SetPrototypeMethod(tpl, "cancel", Cancel);

	auto confun = Nan::GetFunction(tpl).ToLocalChecked();
	constructor.Reset(confun);

	// Add the Transfer constructor to the module
	exports->Set(Nan::New("Transfer").ToLocalChecked(), confun);
}

// new Transfer(device, endpointAddr, type, timeout, callback)
// static
void Transfer::New(const Nan::FunctionCallbackInfo<Value>& args) {
	if (!args.IsConstructCall()) return Nan::ThrowError("Must be called with `new`!");

	if(args.Length() == 0 || !args[0]->IsObject())
		return Nan::ThrowTypeError("The Device you'd like to transfer to must be provided.");
	if(args.Length() == 1 || !args[1]->IsNumber())
		return Nan::ThrowTypeError("The endpoint index must be specified");
	if(args.Length() == 2 || !args[2]->IsNumber())
		return Nan::ThrowTypeError("The type must be specified");
	if(args.Length() == 3 || !args[3]->IsNumber())
		return Nan::ThrowTypeError("A transfer timeout must be specified.");
	if(args.Length() == 4 || !args[4]->IsFunction())
		return Nan::ThrowTypeError("A completion callback must be provided");

	auto self = new Transfer();

	setConst(args.This(), "device", args[0]);

	self->attach(args.This());
	self->device = Device::Unwrap(Nan::To<Object>(args[0]).ToLocalChecked());
	self->transfer->endpoint = Nan::To<int>(args[1]).FromJust();
	self->transfer->type = Nan::To<int>(args[2]).FromJust();
	self->transfer->timeout = Nan::To<int>(args[3]).FromJust();

	self->v8callback.Reset(args[4].As<Function>());

	args.GetReturnValue().Set(args.This());
}

// Transfer.submit(buffer, callback)
// static
void Transfer::Submit(const Nan::FunctionCallbackInfo<Value>& args) {
	if(args.Length() == 0 || !node::Buffer::HasInstance(args[0]))
		return Nan::ThrowTypeError("You must provide a transfer buffer. I mean, if you want to transfer something...");

	Transfer* self = UnwrapTransfer(args.Holder());

	if (!self->device->IsOpen())
		return Nan::ThrowError("The device must be open before you can transfer data to it.");

	if (self->transfer->buffer){
		return Nan::ThrowError("A transfer is already underway for this device.");
	}

	Local<Object> buffer_obj = args[0]->ToObject();

	// Can't be cached in constructor as device could be closed and re-opened
	self->transfer->dev_handle = self->device->DeviceHandle();

	self->v8buffer.Reset(buffer_obj);
	self->transfer->buffer = (unsigned char*)node::Buffer::Data(buffer_obj);
	self->transfer->length = node::Buffer::Length(buffer_obj);

	self->ref();
	//TODO: Device lifetime self->device->ref();

	#ifndef USE_POLL
	completionQueue.ref();
	#endif

	DEBUG_LOG("Submitting, %p %p %x %i %i %i %p",
		self,
		self->transfer->dev_handle,
		self->transfer->endpoint,
		self->transfer->type,
		self->transfer->timeout,
		self->transfer->length,
		self->transfer->buffer
	);

	CHECK_USB(libusb_submit_transfer(self->transfer));

	args.GetReturnValue().Set(args.This());
}

// static
void Transfer::Cancel(const Nan::FunctionCallbackInfo<Value>& args) {
	Transfer* self = UnwrapTransfer(args.Holder());

	DEBUG_LOG("Cancel %p %i", self, !!self->transfer->buffer);

	Local<Boolean> result = Nan::False();

	int r = libusb_cancel_transfer(self->transfer);
	if (r != LIBUSB_ERROR_NOT_FOUND){
		CHECK_USB(r);
		result = Nan::True();
	}

	args.GetReturnValue().Set(result);
}

// static
void Transfer::CompletionCallback(libusb_transfer* transfer) {
	Transfer* t = static_cast<Transfer*>(transfer->user_data);

	DEBUG_LOG("Completion callback %p", t);
	assert(t != NULL);

	t->HandleCompletion();
}

// static
void Transfer::QueueCompletion(libusb_transfer* transfer) {
	completionQueue.post(transfer);
}

void Transfer::HandleCompletion() {
	Nan::HandleScope scope;

	DEBUG_LOG("HandleCompletion %p", this);

	//TODO: Device lifetime this->device->unref();
	#ifndef USE_POLL
	completionQueue.unref();
	#endif

	// The callback may resubmit and overwrite these, so need to clear the
	// persistent first.
	Local<Object> buffer = Nan::New<Object>(this->v8buffer);
	this->v8buffer.Reset();
	this->transfer->buffer = NULL;

	if (!this->v8callback.IsEmpty()) {
		Local<Value> error = Nan::Undefined();
		if (this->transfer->status != 0){
			error = libusbException(this->transfer->status);
		}

		Local<Value> argv[] = {error, buffer,
			Nan::New<Uint32>((uint32_t) this->transfer->actual_length)};

		Nan::TryCatch try_catch;
		Nan::MakeCallback(this->handle(), Nan::New(this->v8callback), 3, argv);
		if (try_catch.HasCaught()) {
			Nan::FatalException(try_catch);
		}
	}

	this->unref();
}

