/**
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed in accordance with the terms specified in
 *  the LICENSE file found in the root directory of this source tree.
 */

#include <locale>
#include <string>

#include <osquery/core/windows/wmi.h>
#include <osquery/logger.h>
#include <osquery/utils/conversions/windows/strings.h>

namespace osquery {

WmiMethodArgs::WmiMethodArgs(WmiMethodArgs&& src) {
  std::swap(arguments, src.arguments);
}

WmiMethodArgs::~WmiMethodArgs() {
  for (const auto& p : arguments) {
    auto var = p.second;

    // BSTR variant types have a raw pointer we need to clean up
    if (var.vt == VT_BSTR && var.bstrVal != nullptr) {
      SysFreeString(var.bstrVal);
      var.bstrVal = nullptr;
    }
  }
}

template <>
Status WmiMethodArgs::Put<unsigned int>(const std::string& name,
                                        const unsigned int& value) {
  VARIANT var;
  var.vt = VT_UI4;
  var.ulVal = value;

  arguments.insert(std::pair<std::string, VARIANT>(name, var));
  return Status::success();
}

template <>
Status WmiMethodArgs::Put<std::string>(const std::string& name,
                                       const std::string& value) {
  auto wide_value = stringToWstring(value);

  VARIANT var;
  var.vt = VT_BSTR;
  var.bstrVal = SysAllocString(wide_value.c_str());
  if (var.bstrVal == nullptr) {
    return Status::failure("Out of memory");
  }

  arguments.insert(std::pair<std::string, VARIANT>(name, var));
  return Status::success();
}

/// Utility function to help turn a property value into BSTR
inline BSTR WbemClassObjectPropToBSTR(const WmiResultItem& item,
                                      const std::string& property) {
  std::string value;
  auto status = item.GetString(property, value);
  if (!status.ok()) {
    return nullptr;
  }

  auto wstr_value = stringToWstring(value);
  return SysAllocString(wstr_value.c_str());
}

void WmiResultItem::PrintType(const std::string& name) const {
  std::wstring property_name = stringToWstring(name);
  VARIANT value;
  HRESULT hr = result_->Get(property_name.c_str(), 0, &value, nullptr, nullptr);
  if (hr != S_OK) {
    std::cerr << "Failed: " << name << "\n";
  } else {
    std::cout << "Name=" << name << ", Type=" << value.vt << "\n";
    if (value.vt == VT_I4) {
      std::cout << "  Value=" << value.lVal << "\n";
    } else if (value.vt == VT_BSTR) {
      std::wcout << "  Value=" << value.bstrVal << "\n";
    }
  }
  VariantClear(&value);
}

Status WmiResultItem::GetBool(const std::string& name, bool& ret) const {
  std::wstring property_name = stringToWstring(name);
  VARIANT value;
  HRESULT hr = result_->Get(property_name.c_str(), 0, &value, nullptr, nullptr);

  if (hr != S_OK) {
    return Status::failure("Error retrieving data from WMI query.");
  }
  if (value.vt != VT_BOOL) {
    VariantClear(&value);
    return Status::failure("Invalid data type returned.");
  }
  ret = value.boolVal == VARIANT_TRUE ? true : false;
  VariantClear(&value);
  return Status::success();
}

Status WmiResultItem::GetDateTime(const std::string& name,
                                  bool is_local,
                                  FILETIME& ft) const {
  std::wstring property_name = stringToWstring(name);

  VARIANT value;
  HRESULT hr = result_->Get(property_name.c_str(), 0, &value, nullptr, nullptr);
  if (hr != S_OK) {
    return Status::failure("Error retrieving datetime from WMI query result.");
  }

  if (value.vt != VT_BSTR) {
    VariantClear(&value);
    return Status::failure("Expected VT_BSTR, got something else.");
  }

  ISWbemDateTime* dt = nullptr;
  hr = CoCreateInstance(
      CLSID_SWbemDateTime, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dt));
  if (!SUCCEEDED(hr)) {
    VariantClear(&value);
    return Status::failure("Failed to create SWbemDateTime object.");
  }

  hr = dt->put_Value(value.bstrVal);
  VariantClear(&value);

  if (!SUCCEEDED(hr)) {
    dt->Release();
    return Status::failure("Failed to set SWbemDateTime value.");
  }

  BSTR filetime_str = {0};
  hr = dt->GetFileTime(is_local ? VARIANT_TRUE : VARIANT_FALSE, &filetime_str);
  if (!SUCCEEDED(hr)) {
    dt->Release();
    return Status::failure("GetFileTime failed.");
  }

  ULARGE_INTEGER ui = {};

  ui.QuadPart = _wtoi64(filetime_str);
  ft.dwLowDateTime = ui.LowPart;
  ft.dwHighDateTime = ui.HighPart;

  SysFreeString(filetime_str);
  dt->Release();

  return Status::success();
}

Status WmiResultItem::GetUChar(const std::string& name,
                               unsigned char& ret) const {
  std::wstring property_name = stringToWstring(name);
  VARIANT value;
  HRESULT hr = result_->Get(property_name.c_str(), 0, &value, nullptr, nullptr);
  if (hr != S_OK) {
    return Status::failure("Error retrieving data from WMI query.");
  }
  if (value.vt != VT_UI1) {
    VariantClear(&value);
    return Status::failure("Invalid data type returned.");
  }
  ret = value.bVal;
  VariantClear(&value);
  return Status::success();
}

Status WmiResultItem::GetUnsignedShort(const std::string& name,
                                       unsigned short& ret) const {
  std::wstring property_name = stringToWstring(name);
  VARIANT value;
  HRESULT hr = result_->Get(property_name.c_str(), 0, &value, nullptr, nullptr);

  if (hr != S_OK) {
    return Status::failure("Error retrieving data from WMI query.");
  }
  if (value.vt != VT_UI2) {
    VariantClear(&value);
    return Status::failure("Invalid data type returned.");
  }
  ret = value.uiVal;
  VariantClear(&value);
  return Status::success();
}

Status WmiResultItem::GetUnsignedInt32(const std::string& name,
                                       unsigned int& ret) const {
  std::wstring property_name = stringToWstring(name);
  VARIANT value;
  HRESULT hr = result_->Get(property_name.c_str(), 0, &value, nullptr, nullptr);

  if (hr != S_OK) {
    return Status::failure("Error retrieving data from WMI query.");
  }
  if (value.vt != VT_UINT) {
    VariantClear(&value);
    return Status::failure("Invalid data type returned.");
  }
  ret = value.uiVal;
  VariantClear(&value);
  return Status::success();
}

Status WmiResultItem::GetLong(const std::string& name, long& ret) const {
  std::wstring property_name = stringToWstring(name);
  VARIANT value;
  HRESULT hr = result_->Get(property_name.c_str(), 0, &value, nullptr, nullptr);
  if (hr != S_OK) {
    return Status::failure("Error retrieving data from WMI query.");
  }
  if (value.vt != VT_I4) {
    VariantClear(&value);
    return Status::failure("Invalid data type returned.");
  }
  ret = value.lVal;
  VariantClear(&value);
  return Status::success();
}

Status WmiResultItem::GetUnsignedLong(const std::string& name,
                                      unsigned long& ret) const {
  std::wstring property_name = stringToWstring(name);
  VARIANT value;
  HRESULT hr = result_->Get(property_name.c_str(), 0, &value, nullptr, nullptr);
  if (hr != S_OK) {
    return Status::failure("Error retrieving data from WMI query.");
  }
  if (value.vt != VT_UI4) {
    VariantClear(&value);
    return Status::failure("Invalid data type returned.");
  }
  ret = value.lVal;
  VariantClear(&value);
  return Status::success();
}

Status WmiResultItem::GetLongLong(const std::string& name,
                                  long long& ret) const {
  std::wstring property_name = stringToWstring(name);
  VARIANT value;
  HRESULT hr = result_->Get(property_name.c_str(), 0, &value, nullptr, nullptr);
  if (hr != S_OK) {
    return Status::failure("Error retrieving data from WMI query.");
  }
  if (value.vt != VT_I8) {
    VariantClear(&value);
    return Status::failure("Invalid data type returned.");
  }
  ret = value.lVal;
  VariantClear(&value);
  return Status::success();
}

Status WmiResultItem::GetUnsignedLongLong(const std::string& name,
                                          unsigned long long& ret) const {
  std::wstring property_name = stringToWstring(name);
  VARIANT value;
  HRESULT hr = result_->Get(property_name.c_str(), 0, &value, nullptr, nullptr);
  if (hr != S_OK) {
    return Status::failure("Error retrieving data from WMI query.");
  }
  if (value.vt != VT_UI8) {
    VariantClear(&value);
    return Status::failure("Invalid data type returned.");
  }
  ret = value.lVal;
  VariantClear(&value);
  return Status::success();
}

Status WmiResultItem::GetString(const std::string& name,
                                std::string& ret) const {
  std::wstring property_name = stringToWstring(name);
  VARIANT value;
  HRESULT hr = result_->Get(property_name.c_str(), 0, &value, nullptr, nullptr);
  if (hr != S_OK) {
    ret = "";
    return Status::failure("Error retrieving data from WMI query.");
  }
  if (value.vt != VT_BSTR) {
    ret = "";
    VariantClear(&value);
    return Status::failure("Invalid data type returned.");
  }
  ret = bstrToString(value.bstrVal);
  VariantClear(&value);
  return Status::success();
}

Status WmiResultItem::GetVectorOfStrings(const std::string& name,
                                         std::vector<std::string>& ret) const {
  std::wstring property_name = stringToWstring(name);
  VARIANT value;
  HRESULT hr = result_->Get(property_name.c_str(), 0, &value, nullptr, nullptr);
  if (hr != S_OK) {
    return Status::failure("Error retrieving data from WMI query.");
  }
  if (value.vt != (VT_BSTR | VT_ARRAY)) {
    VariantClear(&value);
    return Status::failure("Invalid data type returned.");
  }
  long lbound, ubound;
  SafeArrayGetLBound(value.parray, 1, &lbound);
  SafeArrayGetUBound(value.parray, 1, &ubound);
  long count = ubound - lbound + 1;

  BSTR* pData = nullptr;
  SafeArrayAccessData(value.parray, (void**)&pData);
  ret.reserve(count);
  for (long i = 0; i < count; i++) {
    ret.push_back(bstrToString(pData[i]));
  }
  SafeArrayUnaccessData(value.parray);
  VariantClear(&value);
  return Status::success();
}

WmiRequest::WmiRequest(const std::string& query, BSTR nspace) {
  std::wstring wql = stringToWstring(query);

  HRESULT hr = E_FAIL;

  IWbemLocator* locator = nullptr;
  hr = ::CoInitializeSecurity(nullptr,
                              -1,
                              nullptr,
                              nullptr,
                              RPC_C_AUTHN_LEVEL_DEFAULT,
                              RPC_C_IMP_LEVEL_IMPERSONATE,
                              nullptr,
                              EOAC_NONE,
                              nullptr);
  hr = ::CoCreateInstance(CLSID_WbemLocator,
                          0,
                          CLSCTX_INPROC_SERVER,
                          IID_IWbemLocator,
                          (LPVOID*)&locator);
  if (hr != S_OK) {
    return;
  }
  locator_.reset(locator);

  IWbemServices* services = nullptr;
  hr = locator_->ConnectServer(
      nspace, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
  if (hr != S_OK) {
    return;
  }
  services_.reset(services);

  IEnumWbemClassObject* wbem_enum = nullptr;

  hr = services_->ExecQuery((BSTR)L"WQL",
                            (BSTR)wql.c_str(),
                            WBEM_FLAG_FORWARD_ONLY,
                            nullptr,
                            &wbem_enum);
  if (hr != S_OK) {
    return;
  }

  enum_.reset(wbem_enum);

  hr = WBEM_S_NO_ERROR;
  while (hr == WBEM_S_NO_ERROR) {
    IWbemClassObject* result = nullptr;
    ULONG result_count = 0;

    hr = enum_->Next(WBEM_INFINITE, 1, &result, &result_count);
    if (SUCCEEDED(hr) && result_count > 0) {
      results_.emplace_back(result);
    }
  }

  status_ = Status(0);
}

Status WmiRequest::ExecMethod(const WmiResultItem& object,
                              const std::string& method,
                              const WmiMethodArgs& args,
                              WmiResultItem& out_result) const {
  std::wstring property_name = stringToWstring(method);

  IWbemClassObject* raw = nullptr;

  std::unique_ptr<IWbemClassObject, impl::WmiObjectDeleter> in_def{nullptr};
  std::unique_ptr<IWbemClassObject, impl::WmiObjectDeleter> class_obj{nullptr};

  BSTR wmi_class_name = WbemClassObjectPropToBSTR(object, "__CLASS");
  if (wmi_class_name == nullptr) {
    return Status::failure("Class name out of memory");
  }

  // GetObject obtains a CIM Class definition object
  HRESULT hr = services_->GetObject(wmi_class_name, 0, nullptr, &raw, nullptr);
  SysFreeString(wmi_class_name);

  if (FAILED(hr)) {
    return Status::failure("Failed to GetObject");
  }

  class_obj.reset(raw);
  raw = nullptr;

  // GetMethod only works on CIM class definition objects. This is why
  // we don't use result_
  hr = class_obj->GetMethod(property_name.c_str(), 0, &raw, nullptr);
  if (FAILED(hr)) {
    return Status::failure("Failed to GetMethod");
  }

  in_def.reset(raw);
  raw = nullptr;

  std::unique_ptr<IWbemClassObject, impl::WmiObjectDeleter> args_inst{nullptr};

  // in_def can be nullptr if the chosen method has no in-parameters
  if (in_def != nullptr) {
    hr = in_def->SpawnInstance(0, &raw);
    if (FAILED(hr)) {
      return Status::failure("Failed to SpawnInstance");
    }
    args_inst.reset(raw);

    // Build up the WMI method call arguments
    for (const auto& p : args.GetArguments()) {
      const auto& name = p.first;
      auto pVal = p.second;

      auto args_name = stringToWstring(name);

      hr = args_inst->Put(args_name.c_str(), 0, &pVal, 0);
      if (FAILED(hr)) {
        return Status::failure("Failed to Put arguments");
      }
    }
  }

  // In order to execute a WMI method, we need to know the specific object name
  // and method name
  IWbemClassObject* out_params = nullptr;

  auto wmi_meth_name = SysAllocString(property_name.c_str());
  if (wmi_meth_name == nullptr) {
    return Status::failure("Out of memory");
  }

  auto wmi_obj_path = WbemClassObjectPropToBSTR(object, "__PATH");
  if (wmi_obj_path == nullptr) {
    SysFreeString(wmi_meth_name);
    return Status::failure("Out of memory");
  }

  // Execute the WMI method, the return value and out-params all exist in
  // out_params
  hr = services_->ExecMethod(wmi_obj_path,
                             wmi_meth_name,
                             0,
                             nullptr,
                             args_inst.get(),
                             &out_params,
                             nullptr);

  SysFreeString(wmi_meth_name);
  SysFreeString(wmi_obj_path);

  if (FAILED(hr)) {
    return Status::failure("Failed to ExecMethod");
  }

  out_result = std::move(WmiResultItem(out_params));

  return Status::success();
}

} // namespace osquery
