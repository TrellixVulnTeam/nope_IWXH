// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/common/form_data.h"

#include "base/base64.h"
#include "base/pickle.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/autofill/core/common/form_field_data.h"

namespace autofill {

namespace {

const int kPickleVersion = 3;

bool ReadGURL(PickleIterator* iter, GURL* url) {
  std::string spec;
  if (!iter->ReadString(&spec))
    return false;

  *url = GURL(spec);
  return true;
}

void SerializeFormFieldDataVector(const std::vector<FormFieldData>& fields,
                                  Pickle* pickle) {
  pickle->WriteInt(static_cast<int>(fields.size()));
  for (size_t i = 0; i < fields.size(); ++i) {
    SerializeFormFieldData(fields[i], pickle);
  }
}

bool DeserializeFormFieldDataVector(PickleIterator* iter,
                                    std::vector<FormFieldData>* fields) {
  int size;
  if (!iter->ReadInt(&size))
    return false;

  FormFieldData temp;
  for (int i = 0; i < size; ++i) {
    if (!DeserializeFormFieldData(iter, &temp))
      return false;

    fields->push_back(temp);
  }
  return true;
}

void LogDeserializationError(int version) {
  DVLOG(1) << "Could not deserialize version " << version
             << " FormData from pickle.";
}

}  // namespace

FormData::FormData()
    : user_submitted(false),
      is_form_tag(true) {
}

FormData::FormData(const FormData& data)
    : name(data.name),
      origin(data.origin),
      action(data.action),
      user_submitted(data.user_submitted),
      is_form_tag(data.is_form_tag),
      fields(data.fields) {
}

FormData::~FormData() {
}

bool FormData::SameFormAs(const FormData& form) const {
  if (name != form.name ||
      origin != form.origin ||
      action != form.action ||
      user_submitted != form.user_submitted ||
      is_form_tag != form.is_form_tag ||
      fields.size() != form.fields.size())
    return false;
  for (size_t i = 0; i < fields.size(); ++i) {
    if (!fields[i].SameFieldAs(form.fields[i]))
      return false;
  }
  return true;
}

bool FormData::operator<(const FormData& form) const {
  if (name != form.name)
    return name < form.name;
  if (origin != form.origin)
    return origin < form.origin;
  if (action != form.action)
    return action < form.action;
  if (user_submitted != form.user_submitted)
    return user_submitted < form.user_submitted;
  if (is_form_tag != form.is_form_tag)
    return is_form_tag < form.is_form_tag;
  return fields < form.fields;
}

std::ostream& operator<<(std::ostream& os, const FormData& form) {
  os << base::UTF16ToUTF8(form.name) << " "
     << form.origin << " "
     << form.action << " "
     << form.user_submitted << " "
     << form.is_form_tag << " "
     << "Fields:";
  for (size_t i = 0; i < form.fields.size(); ++i) {
    os << form.fields[i] << ",";
  }
  return os;
}

void SerializeFormData(const FormData& form_data, Pickle* pickle) {
  pickle->WriteInt(kPickleVersion);
  pickle->WriteString16(form_data.name);
  pickle->WriteString(form_data.origin.spec());
  pickle->WriteString(form_data.action.spec());
  pickle->WriteBool(form_data.user_submitted);
  SerializeFormFieldDataVector(form_data.fields, pickle);
  pickle->WriteBool(form_data.is_form_tag);
}

void SerializeFormDataToBase64String(const FormData& form_data,
                                     std::string* output) {
  Pickle pickle;
  SerializeFormData(form_data, &pickle);
  Base64Encode(
      base::StringPiece(static_cast<const char*>(pickle.data()), pickle.size()),
      output);
}

bool DeserializeFormData(PickleIterator* iter, FormData* form_data) {
  int version;
  if (!iter->ReadInt(&version)) {
    DVLOG(1) << "Bad pickle of FormData, no version present";
    return false;
  }

  if (version < 1 || version > kPickleVersion) {
    DVLOG(1) << "Unknown FormData pickle version " << version;
    return false;
  }

  if (!iter->ReadString16(&form_data->name)) {
    LogDeserializationError(version);
    return false;
  }

  if (version == 1) {
    base::string16 method;
    if (!iter->ReadString16(&method)) {
      LogDeserializationError(version);
      return false;
    }
  }

  if (!ReadGURL(iter, &form_data->origin) ||
      !ReadGURL(iter, &form_data->action) ||
      !iter->ReadBool(&form_data->user_submitted) ||
      !DeserializeFormFieldDataVector(iter, &form_data->fields)) {
    LogDeserializationError(version);
    return false;
  }

  if (version == 3) {
    if (!iter->ReadBool(&form_data->is_form_tag)) {
      LogDeserializationError(version);
      return false;
    }
  } else {
    form_data->is_form_tag = true;
  }

  return true;
}

bool DeserializeFormDataFromBase64String(const base::StringPiece& input,
                                         FormData* form_data) {
  if (input.empty())
    return false;
  std::string pickle_data;
  Base64Decode(input, &pickle_data);
  Pickle pickle(pickle_data.data(), static_cast<int>(pickle_data.size()));
  PickleIterator iter(pickle);
  return DeserializeFormData(&iter, form_data);
}

}  // namespace autofill
