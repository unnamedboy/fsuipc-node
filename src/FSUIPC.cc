// fsuipc.cc
#include <string>

#include <nan.h>
#include <node.h>

#include <windows.h>
#include "../include/fsuipc/FSUIPC_User64.h"

#include "FSUIPC.h"

namespace FSUIPC {

const char* pszErrors[] = {
    "Okay",
    "Attempt to Open when already Open",
    "Cannot link to FSUIPC or WideClient",
    "Failed to Register common message with Windows",
    "Failed to create Atom for mapping filename",
    "Failed to create a file mapping object",
    "Failed to open a view to the file map",
    "Incorrect version of FSUIPC, or not FSUIPC",
    "Sim is not version requested",
    "Call cannot execute, link not Open",
    "Call cannot execute: no requests accumulated",
    "IPC timed out all retries",
    "IPC sendmessage failed all retries",
    "IPC request contains bad data",
    "Maybe running on WideClient, but FS not running on Server, or wrong "
    "FSUIPC",
    "Read or Write request cannot be added, memory for Process is full",
};

Nan::Persistent<v8::FunctionTemplate> FSUIPC::constructor;
Nan::Persistent<v8::Object> FSUIPCError;

NAN_MODULE_INIT(FSUIPC::Init) {
  v8::Local<v8::FunctionTemplate> ctor =
      Nan::New<v8::FunctionTemplate>(FSUIPC::New);
  constructor.Reset(ctor);
  ctor->InstanceTemplate()->SetInternalFieldCount(1);
  ctor->SetClassName(Nan::New("FSUIPC").ToLocalChecked());

  Nan::SetPrototypeMethod(ctor, "open", Open);
  Nan::SetPrototypeMethod(ctor, "close", Close);

  Nan::SetPrototypeMethod(ctor, "process", Process);

  Nan::SetPrototypeMethod(ctor, "add", Add);
  Nan::SetPrototypeMethod(ctor, "remove", Remove);

  target->Set(Nan::New("FSUIPC").ToLocalChecked(), ctor->GetFunction());
}

NAN_METHOD(FSUIPC::New) {
  // throw an error if constructor is called without new keyword
  if (!info.IsConstructCall()) {
    return Nan::ThrowError(
        Nan::New("FSUIPC.new - called without new keyword").ToLocalChecked());
  }

  if (info.Length() != 0) {
    return Nan::ThrowError(
        Nan::New("FSUIPC.new - expected no arguments").ToLocalChecked());
  }

  FSUIPC* fsuipc = new FSUIPC();
  fsuipc->Wrap(info.Holder());

  info.GetReturnValue().Set(info.Holder());
}

NAN_METHOD(FSUIPC::Open) {
  FSUIPC* self = Nan::ObjectWrap::Unwrap<FSUIPC>(info.This());

  DWORD requestedSim = SIM_ANY;

  if (info.Length() > 0) {
    if (!info[0]->IsUint32()) {
      return Nan::ThrowTypeError(
          Nan::New("FSUIPC.open - expected first argument to be Simulator")
              .ToLocalChecked());
    }

    requestedSim = (DWORD) info[0]->Uint32Value();
  }

  auto worker = new OpenAsyncWorker(self, requestedSim);

  PromiseQueueWorker(worker);

  info.GetReturnValue().Set(worker->GetPromise());
}

NAN_METHOD(FSUIPC::Close) {
  FSUIPC* self = Nan::ObjectWrap::Unwrap<FSUIPC>(info.This());

  auto worker = new CloseAsyncWorker(self);

  PromiseQueueWorker(worker);

  info.GetReturnValue().Set(worker->GetPromise());
}

NAN_METHOD(FSUIPC::Process) {
  FSUIPC* self = Nan::ObjectWrap::Unwrap<FSUIPC>(info.This());

  auto worker = new ProcessAsyncWorker(self);

  PromiseQueueWorker(worker);

  info.GetReturnValue().Set(worker->GetPromise());
}

NAN_METHOD(FSUIPC::Add) {
  FSUIPC* self = Nan::ObjectWrap::Unwrap<FSUIPC>(info.This());

  if (info.Length() < 3) {
    return Nan::ThrowError(
        Nan::New("FSUIPC.Add: requires at least 3 arguments").ToLocalChecked());
  }

  if (!info[0]->IsString()) {
    return Nan::ThrowTypeError(
        Nan::New("FSUIPC.Add: expected first argument to be string")
            .ToLocalChecked());
  }

  if (!info[1]->IsUint32()) {
    return Nan::ThrowTypeError(
        Nan::New("FSUIPC.Add: expected second argument to be uint")
            .ToLocalChecked());
  }

  if (!info[2]->IsInt32()) {
    return Nan::ThrowTypeError(
        Nan::New("FSUIPC.Add: expected third argument to be int")
            .ToLocalChecked());
  }

  std::string name = std::string(*Nan::Utf8String(info[0]));
  DWORD offset = info[1]->Uint32Value();
  Type type = (Type)info[2]->Int32Value();

  DWORD size;

  if (type == Type::ByteArray || type == Type::BitArray ||
      type == Type::String) {
    if (info.Length() < 4) {
      return Nan::ThrowTypeError(
          Nan::New("FSUIPC.Add: requires at least 4 arguments if type is "
                   "byteArray, bitArray or string")
              .ToLocalChecked());
    }

    if (!info[3]->IsUint32()) {
      return Nan::ThrowTypeError(
          Nan::New("FSUIPC.Add: expected fourth argument to be uint")
              .ToLocalChecked());
    }

    size = (int)info[3]->Uint32Value();
  } else {
    size = get_size_of_type(type);
  }

  if (size == 0) {
    return Nan::ThrowTypeError(
        Nan::New("FSUIPC.Add: expected third argument to be a type")
            .ToLocalChecked());
  }

  self->offsets[name] = Offset{name, type, offset, size, malloc(size)};

  v8::Local<v8::Object> obj = Nan::New<v8::Object>();

  Nan::Set(obj, Nan::New("name").ToLocalChecked(),
           Nan::New(name).ToLocalChecked());
  Nan::Set(obj, Nan::New("offset").ToLocalChecked(), info[1]);
  Nan::Set(obj, Nan::New("type").ToLocalChecked(), Nan::New((int)type));
  Nan::Set(obj, Nan::New("size").ToLocalChecked(), Nan::New((int)size));

  info.GetReturnValue().Set(obj);
}

NAN_METHOD(FSUIPC::Remove) {
  FSUIPC* self = Nan::ObjectWrap::Unwrap<FSUIPC>(info.This());

  if (info.Length() != 1) {
    return Nan::ThrowError(
        Nan::New("FSUIPC.Remove: requires one argument").ToLocalChecked());
  }

  if (!info[0]->IsString()) {
    return Nan::ThrowTypeError(
        Nan::New("FSUIPC.Remove: expected first argument to be string")
            .ToLocalChecked());
  }

  std::string name = std::string(*Nan::Utf8String(info[0]));

  auto it = self->offsets.find(name);

  v8::Local<v8::Object> obj = Nan::New<v8::Object>();

  Nan::Set(obj, Nan::New("name").ToLocalChecked(),
           Nan::New(it->second.name).ToLocalChecked());
  Nan::Set(obj, Nan::New("offset").ToLocalChecked(),
           Nan::New((int)it->second.offset));
  Nan::Set(obj, Nan::New("type").ToLocalChecked(),
           Nan::New((int)it->second.type));
  Nan::Set(obj, Nan::New("size").ToLocalChecked(),
           Nan::New((int)it->second.size));

  self->offsets.erase(it);

  info.GetReturnValue().Set(obj);
}

void ProcessAsyncWorker::Execute() {
  DWORD dwResult;

  std::lock_guard<std::mutex> guard(this->fsuipc->offsets_mutex);

  auto offsets = this->fsuipc->offsets;

  std::map<std::string, Offset>::iterator it = offsets.begin();

  for (; it != offsets.end(); ++it) {
    if (!FSUIPC_Read(it->second.offset, it->second.size, it->second.dest,
                     &dwResult)) {
      this->SetErrorMessage(pszErrors[dwResult]);
      this->errorCode = (int)dwResult;
      return;
    }
  }

  if (!FSUIPC_Process(&dwResult)) {
    this->SetErrorMessage(pszErrors[dwResult]);
    this->errorCode = (int)dwResult;
    return;
  }
}

void ProcessAsyncWorker::HandleOKCallback() {
  Nan::HandleScope scope;

  std::lock_guard<std::mutex> guard(this->fsuipc->offsets_mutex);

  auto offsets = this->fsuipc->offsets;

  v8::Local<v8::Object> obj = Nan::New<v8::Object>();
  std::map<std::string, Offset>::iterator it = offsets.begin();

  for (; it != offsets.end(); ++it) {
    Nan::Set(obj, Nan::New(it->second.name).ToLocalChecked(),
             this->GetValue(it->second.type, it->second.dest, it->second.size));
  }

  Nan::New(resolver)->Resolve(obj);
}

void ProcessAsyncWorker::HandleErrorCallback() {
  Nan::HandleScope scope;

  v8::Local<v8::Value> argv[] = {Nan::New(ErrorMessage()).ToLocalChecked(),
                                 Nan::New(this->errorCode)};
  v8::Local<v8::Value> error =
      Nan::CallAsConstructor(Nan::New(FSUIPCError), 2, argv).ToLocalChecked();

  Nan::New(resolver)->Reject(error);
}

v8::Local<v8::Value> ProcessAsyncWorker::GetValue(Type type,
                                                  void* data,
                                                  size_t length) {
  Nan::EscapableHandleScope scope;

  switch (type) {
    case Type::Byte:
      return scope.Escape(Nan::New(*((uint8_t*)data)));
    case Type::SByte:
      return scope.Escape(Nan::New(*((int8_t*)data)));
    case Type::Int16:
      return scope.Escape(Nan::New(*((int16_t*)data)));
    case Type::Int32:
      return scope.Escape(Nan::New(*((int32_t*)data)));
    case Type::Int64:
      return scope.Escape(
          Nan::New(std::to_string(*((int64_t*)data))).ToLocalChecked());
    case Type::UInt16:
      return scope.Escape(Nan::New(*((uint16_t*)data)));
    case Type::UInt32:
      return scope.Escape(Nan::New(*((uint32_t*)data)));
    case Type::UInt64:
      return scope.Escape(
          Nan::New(std::to_string(*((uint64_t*)data))).ToLocalChecked());
    case Type::Double:
      return scope.Escape(Nan::New(*((double*)data)));
    case Type::Single:
      return scope.Escape(Nan::New(*((float*)data)));
    case Type::String: {
      char* str = (char*)data;
      return scope.Escape(Nan::New(str).ToLocalChecked());
    }
    case Type::BitArray: {
      v8::Local<v8::Array> arr = Nan::New<v8::Array>(length * 8);
      uint8_t* bits = (uint8_t*)data;
      for (int i = 0; i < length * 8; i++) {
        int byte_index = i / 8;
        int bit_index = i % 8;
        int mask = 1 << bit_index;
        bool value = (bits[byte_index] & mask) != 0;
        arr->Set(i, Nan::New(value));
      }
      return scope.Escape(arr);
    }
    case Type::ByteArray: {
      v8::Local<v8::Array> arr = Nan::New<v8::Array>(length);
      uint8_t* bytes = (uint8_t*)data;
      for (int i = 0; i < length; i++) {
        arr->Set(i, Nan::New(*bytes));
        bytes++;
      }
      return scope.Escape(arr);
    }
  }

  return scope.Escape(Nan::Undefined());
}

DWORD get_size_of_type(Type type) {
  switch (type) {
    case Type::Byte:
    case Type::SByte:
      return 1;
    case Type::Int16:
    case Type::UInt16:
      return 2;
    case Type::Int32:
    case Type::UInt32:
      return 4;
    case Type::Int64:
    case Type::UInt64:
      return 8;
    case Type::Double:
      return 8;
    case Type::Single:
      return 4;
  }
  return 0;
}

void OpenAsyncWorker::Execute() {
  DWORD dwResult;

  if (!FSUIPC_Open(this->requestedSim, &dwResult)) {
    this->SetErrorMessage(pszErrors[dwResult]);
    this->errorCode = (int) dwResult;
    return;
  }
}

void OpenAsyncWorker::HandleOKCallback() {
  Nan::HandleScope scope;

  Nan::New(resolver)->Resolve(this->fsuipc->handle());
}

void OpenAsyncWorker::HandleErrorCallback() {
  Nan::HandleScope scope;

  v8::Local<v8::Value> argv[] = {Nan::New(ErrorMessage()).ToLocalChecked(),
                                 Nan::New(this->errorCode)};
  v8::Local<v8::Value> error =
      Nan::CallAsConstructor(Nan::New(FSUIPCError), 2, argv).ToLocalChecked();

  Nan::New(resolver)->Reject(error);
}

void CloseAsyncWorker::Execute() {
  FSUIPC_Close();
}

void CloseAsyncWorker::HandleOKCallback() {
  Nan::HandleScope scope;

  Nan::New(resolver)->Resolve(this->fsuipc->handle());
}

NAN_MODULE_INIT(InitType) {
  v8::Local<v8::Object> obj = Nan::New<v8::Object>();
  Nan::DefineOwnProperty(obj, Nan::New("Byte").ToLocalChecked(),
                         Nan::New((int)Type::Byte), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("SByte").ToLocalChecked(),
                         Nan::New((int)Type::SByte), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("Int16").ToLocalChecked(),
                         Nan::New((int)Type::Int16), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("Int32").ToLocalChecked(),
                         Nan::New((int)Type::Int32), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("Int64").ToLocalChecked(),
                         Nan::New((int)Type::Int64), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("UInt16").ToLocalChecked(),
                         Nan::New((int)Type::UInt16), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("UInt32").ToLocalChecked(),
                         Nan::New((int)Type::UInt32), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("UInt64").ToLocalChecked(),
                         Nan::New((int)Type::UInt64), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("Double").ToLocalChecked(),
                         Nan::New((int)Type::Double), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("Single").ToLocalChecked(),
                         Nan::New((int)Type::Single), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("ByteArray").ToLocalChecked(),
                         Nan::New((int)Type::ByteArray), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("String").ToLocalChecked(),
                         Nan::New((int)Type::String), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("BitArray").ToLocalChecked(),
                         Nan::New((int)Type::BitArray), v8::ReadOnly);

  target->Set(Nan::New("Type").ToLocalChecked(), obj);
}

NAN_MODULE_INIT(InitError) {
  std::string code =
      "class FSUIPCError extends Error {"
      "  constructor (message, code) {"
      "    super(message);"
      "    this.name = this.constructor.name;"
      "    Error.captureStackTrace(this, this.constructor);"
      "    this.code = code;"
      "  }"
      "};"
      "FSUIPCError";
  v8::Local<Nan::BoundScript> script =
      Nan::CompileScript(Nan::New(code).ToLocalChecked()).ToLocalChecked();
  v8::Local<v8::Object> errorFunc =
      Nan::RunScript(script).ToLocalChecked()->ToObject();

  FSUIPCError.Reset(errorFunc);

  target->Set(Nan::New("FSUIPCError").ToLocalChecked(), errorFunc);

  v8::Local<v8::Object> obj = Nan::New<v8::Object>();
  Nan::DefineOwnProperty(obj, Nan::New("OK").ToLocalChecked(),
                         Nan::New(FSUIPC_ERR_OK), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("OPEN").ToLocalChecked(),
                         Nan::New(FSUIPC_ERR_OPEN), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("NOFS").ToLocalChecked(),
                         Nan::New(FSUIPC_ERR_NOFS), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("REGMSG").ToLocalChecked(),
                         Nan::New(FSUIPC_ERR_REGMSG), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("ATOM").ToLocalChecked(),
                         Nan::New(FSUIPC_ERR_ATOM), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("MAP").ToLocalChecked(),
                         Nan::New(FSUIPC_ERR_MAP), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("VIEW").ToLocalChecked(),
                         Nan::New(FSUIPC_ERR_VIEW), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("VERSION").ToLocalChecked(),
                         Nan::New(FSUIPC_ERR_VERSION), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("WRONGFS").ToLocalChecked(),
                         Nan::New(FSUIPC_ERR_WRONGFS), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("NOTOPEN").ToLocalChecked(),
                         Nan::New(FSUIPC_ERR_NOTOPEN), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("NODATA").ToLocalChecked(),
                         Nan::New(FSUIPC_ERR_NODATA), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("TIMEOUT").ToLocalChecked(),
                         Nan::New(FSUIPC_ERR_TIMEOUT), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("SENDMSG").ToLocalChecked(),
                         Nan::New(FSUIPC_ERR_SENDMSG), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("DATA").ToLocalChecked(),
                         Nan::New(FSUIPC_ERR_DATA), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("RUNNING").ToLocalChecked(),
                         Nan::New(FSUIPC_ERR_RUNNING), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("SIZE").ToLocalChecked(),
                         Nan::New(FSUIPC_ERR_SIZE), v8::ReadOnly);

  target->Set(Nan::New("ErrorCode").ToLocalChecked(), obj);
}

NAN_MODULE_INIT(InitSimulator) {
  v8::Local<v8::Object> obj = Nan::New<v8::Object>();
  Nan::DefineOwnProperty(obj, Nan::New("ANY").ToLocalChecked(),
                         Nan::New(SIM_ANY), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("FS98").ToLocalChecked(),
                         Nan::New(SIM_FS98), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("FS2K").ToLocalChecked(),
                         Nan::New(SIM_FS2K), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("CFS2").ToLocalChecked(),
                         Nan::New(SIM_CFS2), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("CFS1").ToLocalChecked(),
                         Nan::New(SIM_CFS1), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("FLY").ToLocalChecked(),
                         Nan::New(SIM_FLY), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("FS2K2").ToLocalChecked(),
                         Nan::New(SIM_FS2K2), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("FS2K4").ToLocalChecked(),
                         Nan::New(SIM_FS2K4), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("FSX").ToLocalChecked(),
                         Nan::New(SIM_FSX), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("ESP").ToLocalChecked(),
                         Nan::New(SIM_ESP), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("P3D").ToLocalChecked(),
                         Nan::New(SIM_P3D), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("FSX64").ToLocalChecked(),
                         Nan::New(SIM_FSX64), v8::ReadOnly);
  Nan::DefineOwnProperty(obj, Nan::New("P3D64").ToLocalChecked(),
                         Nan::New(SIM_P3D64), v8::ReadOnly);

  target->Set(Nan::New("Simulator").ToLocalChecked(), obj);
}

}  // namespace FSUIPC
