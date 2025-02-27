/*
 * Copyright (c) 2009-2021, Google LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Google LLC nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL Google LLC BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "upb/reflection/message_def.h"

#include "upb/mini_table.h"
#include "upb/reflection/def_builder.h"
#include "upb/reflection/def_type.h"
#include "upb/reflection/enum_def.h"
#include "upb/reflection/extension_range.h"
#include "upb/reflection/field_def.h"
#include "upb/reflection/file_def.h"
#include "upb/reflection/mini_descriptor_encode.h"
#include "upb/reflection/oneof_def.h"

// Must be last.
#include "upb/port_def.inc"

struct upb_MessageDef {
  const google_protobuf_MessageOptions* opts;
  const upb_MiniTable* layout;
  const upb_FileDef* file;
  const upb_MessageDef* containing_type;
  const char* full_name;

  // Tables for looking up fields by number and name.
  upb_inttable itof;
  upb_strtable ntof;

  /* All nested defs.
   * MEM: We could save some space here by putting nested defs in a contiguous
   * region and calculating counts from offsets or vice-versa. */
  const upb_FieldDef* fields;
  const upb_OneofDef* oneofs;
  const upb_ExtensionRange* ext_ranges;
  const upb_MessageDef* nested_msgs;
  const upb_EnumDef* nested_enums;
  const upb_FieldDef* nested_exts;
  int field_count;
  int real_oneof_count;
  int oneof_count;
  int ext_range_count;
  int nested_msg_count;
  int nested_enum_count;
  int nested_ext_count;
  bool in_message_set;
  bool is_sorted;
  upb_WellKnown well_known_type;
#if UINTPTR_MAX == 0xffffffff
  uint32_t padding;  // Increase size to a multiple of 8.
#endif
};

static void assign_msg_wellknowntype(upb_MessageDef* m) {
  const char* name = upb_MessageDef_FullName(m);
  if (name == NULL) {
    m->well_known_type = kUpb_WellKnown_Unspecified;
    return;
  }
  if (!strcmp(name, "google.protobuf.Any")) {
    m->well_known_type = kUpb_WellKnown_Any;
  } else if (!strcmp(name, "google.protobuf.FieldMask")) {
    m->well_known_type = kUpb_WellKnown_FieldMask;
  } else if (!strcmp(name, "google.protobuf.Duration")) {
    m->well_known_type = kUpb_WellKnown_Duration;
  } else if (!strcmp(name, "google.protobuf.Timestamp")) {
    m->well_known_type = kUpb_WellKnown_Timestamp;
  } else if (!strcmp(name, "google.protobuf.DoubleValue")) {
    m->well_known_type = kUpb_WellKnown_DoubleValue;
  } else if (!strcmp(name, "google.protobuf.FloatValue")) {
    m->well_known_type = kUpb_WellKnown_FloatValue;
  } else if (!strcmp(name, "google.protobuf.Int64Value")) {
    m->well_known_type = kUpb_WellKnown_Int64Value;
  } else if (!strcmp(name, "google.protobuf.UInt64Value")) {
    m->well_known_type = kUpb_WellKnown_UInt64Value;
  } else if (!strcmp(name, "google.protobuf.Int32Value")) {
    m->well_known_type = kUpb_WellKnown_Int32Value;
  } else if (!strcmp(name, "google.protobuf.UInt32Value")) {
    m->well_known_type = kUpb_WellKnown_UInt32Value;
  } else if (!strcmp(name, "google.protobuf.BoolValue")) {
    m->well_known_type = kUpb_WellKnown_BoolValue;
  } else if (!strcmp(name, "google.protobuf.StringValue")) {
    m->well_known_type = kUpb_WellKnown_StringValue;
  } else if (!strcmp(name, "google.protobuf.BytesValue")) {
    m->well_known_type = kUpb_WellKnown_BytesValue;
  } else if (!strcmp(name, "google.protobuf.Value")) {
    m->well_known_type = kUpb_WellKnown_Value;
  } else if (!strcmp(name, "google.protobuf.ListValue")) {
    m->well_known_type = kUpb_WellKnown_ListValue;
  } else if (!strcmp(name, "google.protobuf.Struct")) {
    m->well_known_type = kUpb_WellKnown_Struct;
  } else {
    m->well_known_type = kUpb_WellKnown_Unspecified;
  }
}

upb_MessageDef* _upb_MessageDef_At(const upb_MessageDef* m, int i) {
  return (upb_MessageDef*)&m[i];
}

bool _upb_MessageDef_IsValidExtensionNumber(const upb_MessageDef* m, int n) {
  for (int i = 0; i < m->ext_range_count; i++) {
    const upb_ExtensionRange* r = upb_MessageDef_ExtensionRange(m, i);
    if (upb_ExtensionRange_Start(r) <= n && n < upb_ExtensionRange_End(r)) {
      return true;
    }
  }
  return false;
}

bool _upb_MessageDef_IsSorted(const upb_MessageDef* m) { return m->is_sorted; }

const google_protobuf_MessageOptions* upb_MessageDef_Options(
    const upb_MessageDef* m) {
  return m->opts;
}

bool upb_MessageDef_HasOptions(const upb_MessageDef* m) {
  return m->opts != (void*)kUpbDefOptDefault;
}

const char* upb_MessageDef_FullName(const upb_MessageDef* m) {
  return m->full_name;
}

const upb_FileDef* upb_MessageDef_File(const upb_MessageDef* m) {
  return m->file;
}

const upb_MessageDef* upb_MessageDef_ContainingType(const upb_MessageDef* m) {
  return m->containing_type;
}

const char* upb_MessageDef_Name(const upb_MessageDef* m) {
  return _upb_DefBuilder_FullToShort(m->full_name);
}

upb_Syntax upb_MessageDef_Syntax(const upb_MessageDef* m) {
  return upb_FileDef_Syntax(m->file);
}

const upb_FieldDef* upb_MessageDef_FindFieldByNumber(const upb_MessageDef* m,
                                                     uint32_t i) {
  upb_value val;
  return upb_inttable_lookup(&m->itof, i, &val) ? upb_value_getconstptr(val)
                                                : NULL;
}

const upb_FieldDef* upb_MessageDef_FindFieldByNameWithSize(
    const upb_MessageDef* m, const char* name, size_t size) {
  upb_value val;

  if (!upb_strtable_lookup2(&m->ntof, name, size, &val)) {
    return NULL;
  }

  return _upb_DefType_Unpack(val, UPB_DEFTYPE_FIELD);
}

const upb_OneofDef* upb_MessageDef_FindOneofByNameWithSize(
    const upb_MessageDef* m, const char* name, size_t size) {
  upb_value val;

  if (!upb_strtable_lookup2(&m->ntof, name, size, &val)) {
    return NULL;
  }

  return _upb_DefType_Unpack(val, UPB_DEFTYPE_ONEOF);
}

bool _upb_MessageDef_Insert(upb_MessageDef* m, const char* name, size_t len,
                            upb_value v, upb_Arena* a) {
  return upb_strtable_insert(&m->ntof, name, len, v, a);
}

bool upb_MessageDef_FindByNameWithSize(const upb_MessageDef* m,
                                       const char* name, size_t len,
                                       const upb_FieldDef** out_f,
                                       const upb_OneofDef** out_o) {
  upb_value val;

  if (!upb_strtable_lookup2(&m->ntof, name, len, &val)) {
    return false;
  }

  const upb_FieldDef* f = _upb_DefType_Unpack(val, UPB_DEFTYPE_FIELD);
  const upb_OneofDef* o = _upb_DefType_Unpack(val, UPB_DEFTYPE_ONEOF);
  if (out_f) *out_f = f;
  if (out_o) *out_o = o;
  return f || o; /* False if this was a JSON name. */
}

const upb_FieldDef* upb_MessageDef_FindByJsonNameWithSize(
    const upb_MessageDef* m, const char* name, size_t size) {
  upb_value val;
  const upb_FieldDef* f;

  if (!upb_strtable_lookup2(&m->ntof, name, size, &val)) {
    return NULL;
  }

  f = _upb_DefType_Unpack(val, UPB_DEFTYPE_FIELD);
  if (!f) f = _upb_DefType_Unpack(val, UPB_DEFTYPE_FIELD_JSONNAME);

  return f;
}

int upb_MessageDef_ExtensionRangeCount(const upb_MessageDef* m) {
  return m->ext_range_count;
}

int upb_MessageDef_FieldCount(const upb_MessageDef* m) {
  return m->field_count;
}

int upb_MessageDef_OneofCount(const upb_MessageDef* m) {
  return m->oneof_count;
}

int upb_MessageDef_NestedMessageCount(const upb_MessageDef* m) {
  return m->nested_msg_count;
}

int upb_MessageDef_NestedEnumCount(const upb_MessageDef* m) {
  return m->nested_enum_count;
}

int upb_MessageDef_NestedExtensionCount(const upb_MessageDef* m) {
  return m->nested_ext_count;
}

const upb_MiniTable* upb_MessageDef_MiniTable(const upb_MessageDef* m) {
  return m->layout;
}

const upb_ExtensionRange* upb_MessageDef_ExtensionRange(const upb_MessageDef* m,
                                                        int i) {
  UPB_ASSERT(0 <= i && i < m->ext_range_count);
  return _upb_ExtensionRange_At(m->ext_ranges, i);
}

const upb_FieldDef* upb_MessageDef_Field(const upb_MessageDef* m, int i) {
  UPB_ASSERT(0 <= i && i < m->field_count);
  return _upb_FieldDef_At(m->fields, i);
}

const upb_OneofDef* upb_MessageDef_Oneof(const upb_MessageDef* m, int i) {
  UPB_ASSERT(0 <= i && i < m->oneof_count);
  return _upb_OneofDef_At(m->oneofs, i);
}

const upb_MessageDef* upb_MessageDef_NestedMessage(const upb_MessageDef* m,
                                                   int i) {
  UPB_ASSERT(0 <= i && i < m->nested_msg_count);
  return &m->nested_msgs[i];
}

const upb_EnumDef* upb_MessageDef_NestedEnum(const upb_MessageDef* m, int i) {
  UPB_ASSERT(0 <= i && i < m->nested_enum_count);
  return _upb_EnumDef_At(m->nested_enums, i);
}

const upb_FieldDef* upb_MessageDef_NestedExtension(const upb_MessageDef* m,
                                                   int i) {
  UPB_ASSERT(0 <= i && i < m->nested_ext_count);
  return _upb_FieldDef_At(m->nested_exts, i);
}

upb_WellKnown upb_MessageDef_WellKnownType(const upb_MessageDef* m) {
  return m->well_known_type;
}

bool _upb_MessageDef_InMessageSet(const upb_MessageDef* m) {
  return m->in_message_set;
}

const upb_FieldDef* upb_MessageDef_FindFieldByName(const upb_MessageDef* m,
                                                   const char* name) {
  return upb_MessageDef_FindFieldByNameWithSize(m, name, strlen(name));
}

const upb_OneofDef* upb_MessageDef_FindOneofByName(const upb_MessageDef* m,
                                                   const char* name) {
  return upb_MessageDef_FindOneofByNameWithSize(m, name, strlen(name));
}

bool upb_MessageDef_IsMapEntry(const upb_MessageDef* m) {
  return google_protobuf_MessageOptions_map_entry(upb_MessageDef_Options(m));
}

bool upb_MessageDef_IsMessageSet(const upb_MessageDef* m) {
  return google_protobuf_MessageOptions_message_set_wire_format(
      upb_MessageDef_Options(m));
}

static upb_MiniTable* _upb_MessageDef_MakeMiniTable(upb_DefBuilder* ctx,
                                                    const upb_MessageDef* m) {
  if (google_protobuf_MessageOptions_message_set_wire_format(m->opts)) {
    return upb_MiniTable_BuildMessageSet(kUpb_MiniTablePlatform_Native,
                                         ctx->arena);
  }

  if (upb_MessageDef_IsMapEntry(m)) {
    if (m->field_count != 2) {
      _upb_DefBuilder_Errf(ctx, "invalid map (%s)", m->full_name);
    }

    const upb_FieldDef* f0 = upb_MessageDef_Field(m, 0);
    const upb_FieldDef* f1 = upb_MessageDef_Field(m, 1);
    const upb_FieldType t0 = upb_FieldDef_Type(f0);
    const upb_FieldType t1 = upb_FieldDef_Type(f1);

    const bool is_proto3_enum =
        (t1 == kUpb_FieldType_Enum) && !_upb_FieldDef_IsClosedEnum(f1);
    UPB_ASSERT(_upb_FieldDef_LayoutIndex(f0) == 0);
    UPB_ASSERT(_upb_FieldDef_LayoutIndex(f1) == 1);

    return upb_MiniTable_BuildMapEntry(
        t0, t1, is_proto3_enum, kUpb_MiniTablePlatform_Native, ctx->arena);
  }

  upb_StringView desc;
  bool ok = upb_MiniDescriptor_EncodeMessage(m, ctx->tmp_arena, &desc);
  if (!ok) _upb_DefBuilder_OomErr(ctx);

  void** scratch_data = _upb_DefPool_ScratchData(ctx->symtab);
  size_t* scratch_size = _upb_DefPool_ScratchSize(ctx->symtab);
  upb_MiniTable* ret = upb_MiniTable_BuildWithBuf(
      desc.data, desc.size, kUpb_MiniTablePlatform_Native, ctx->arena,
      scratch_data, scratch_size, ctx->status);
  if (!ret) _upb_DefBuilder_FailJmp(ctx);
  return ret;
}

void _upb_MessageDef_Resolve(upb_DefBuilder* ctx, upb_MessageDef* m) {
  for (int i = 0; i < m->field_count; i++) {
    upb_FieldDef* f = (upb_FieldDef*)upb_MessageDef_Field(m, i);
    _upb_FieldDef_Resolve(ctx, upb_MessageDef_FullName(m), f);
  }

  if (!ctx->layout) {
    m->layout = _upb_MessageDef_MakeMiniTable(ctx, m);
    if (!m->layout) _upb_DefBuilder_OomErr(ctx);
  }

  m->in_message_set = false;
  for (int i = 0; i < upb_MessageDef_NestedExtensionCount(m); i++) {
    upb_FieldDef* ext = (upb_FieldDef*)upb_MessageDef_NestedExtension(m, i);
    _upb_FieldDef_Resolve(ctx, upb_MessageDef_FullName(m), ext);
    if (upb_FieldDef_Type(ext) == kUpb_FieldType_Message &&
        upb_FieldDef_Label(ext) == kUpb_Label_Optional &&
        upb_FieldDef_MessageSubDef(ext) == m &&
        google_protobuf_MessageOptions_message_set_wire_format(
            upb_MessageDef_Options(upb_FieldDef_ContainingType(ext)))) {
      m->in_message_set = true;
    }
  }

  for (int i = 0; i < upb_MessageDef_NestedMessageCount(m); i++) {
    upb_MessageDef* n = (upb_MessageDef*)upb_MessageDef_NestedMessage(m, i);
    _upb_MessageDef_Resolve(ctx, n);
  }
}

void _upb_MessageDef_InsertField(upb_DefBuilder* ctx, upb_MessageDef* m,
                                 const upb_FieldDef* f) {
  const int32_t field_number = upb_FieldDef_Number(f);

  if (field_number <= 0 || field_number > kUpb_MaxFieldNumber) {
    _upb_DefBuilder_Errf(ctx, "invalid field number (%u)", field_number);
  }

  const char* json_name = upb_FieldDef_JsonName(f);
  const char* shortname = upb_FieldDef_Name(f);
  const size_t shortnamelen = strlen(shortname);

  upb_value v = upb_value_constptr(f);

  upb_value existing_v;
  if (upb_strtable_lookup(&m->ntof, shortname, &existing_v)) {
    _upb_DefBuilder_Errf(ctx, "duplicate field name (%s)", shortname);
  }

  const upb_value field_v = _upb_DefType_Pack(f, UPB_DEFTYPE_FIELD);
  bool ok =
      _upb_MessageDef_Insert(m, shortname, shortnamelen, field_v, ctx->arena);
  if (!ok) _upb_DefBuilder_OomErr(ctx);

  if (strcmp(shortname, json_name) != 0) {
    if (upb_strtable_lookup(&m->ntof, json_name, &v)) {
      _upb_DefBuilder_Errf(ctx, "duplicate json_name (%s)", json_name);
    }

    const size_t json_size = strlen(json_name);
    const upb_value json_v = _upb_DefType_Pack(f, UPB_DEFTYPE_FIELD_JSONNAME);
    ok = _upb_MessageDef_Insert(m, json_name, json_size, json_v, ctx->arena);
    if (!ok) _upb_DefBuilder_OomErr(ctx);
  }

  if (upb_inttable_lookup(&m->itof, field_number, NULL)) {
    _upb_DefBuilder_Errf(ctx, "duplicate field number (%u)", field_number);
  }

  ok = upb_inttable_insert(&m->itof, field_number, v, ctx->arena);
  if (!ok) _upb_DefBuilder_OomErr(ctx);
}

void _upb_MessageDef_LinkMiniTable(upb_DefBuilder* ctx,
                                   const upb_MessageDef* m) {
  for (int i = 0; i < m->field_count; i++) {
    const upb_FieldDef* f = upb_MessageDef_Field(m, i);
    const upb_MessageDef* sub_m = upb_FieldDef_MessageSubDef(f);
    const upb_EnumDef* sub_e = upb_FieldDef_EnumSubDef(f);
    const int layout_index = _upb_FieldDef_LayoutIndex(f);
    upb_MiniTable* mt = (upb_MiniTable*)upb_MessageDef_MiniTable(m);

    UPB_ASSERT(layout_index < m->field_count);
    upb_MiniTable_Field* mt_f =
        (upb_MiniTable_Field*)&m->layout->fields[layout_index];
    if (sub_m) {
      if (!mt->subs) {
        _upb_DefBuilder_Errf(ctx, "invalid submsg for (%s)", m->full_name);
      }
      UPB_ASSERT(mt_f);
      UPB_ASSERT(sub_m->layout);
      upb_MiniTable_SetSubMessage(mt, mt_f, sub_m->layout);
    } else if (_upb_FieldDef_IsClosedEnum(f)) {
      upb_MiniTable_SetSubEnum(mt, mt_f, _upb_EnumDef_MiniTable(sub_e));
    }
  }

  for (int i = 0; i < m->nested_msg_count; i++) {
    _upb_MessageDef_LinkMiniTable(ctx, upb_MessageDef_NestedMessage(m, i));
  }
}

uint64_t _upb_MessageDef_Modifiers(const upb_MessageDef* m) {
  uint64_t out = 0;
  if (upb_FileDef_Syntax(m->file) == kUpb_Syntax_Proto3) {
    out |= kUpb_MessageModifier_ValidateUtf8;
    out |= kUpb_MessageModifier_DefaultIsPacked;
  }
  if (m->ext_range_count) {
    out |= kUpb_MessageModifier_IsExtendable;
  }
  return out;
}

static void create_msgdef(upb_DefBuilder* ctx, const char* prefix,
                          const google_protobuf_DescriptorProto* msg_proto,
                          const upb_MessageDef* containing_type,
                          upb_MessageDef* m) {
  const google_protobuf_OneofDescriptorProto* const* oneofs;
  const google_protobuf_FieldDescriptorProto* const* fields;
  const google_protobuf_DescriptorProto_ExtensionRange* const* ext_ranges;
  size_t n_oneof, n_field, n_ext_range, n_enum, n_ext, n_msg;
  upb_StringView name;

  // Must happen before _upb_DefBuilder_Add()
  m->file = _upb_DefBuilder_File(ctx);

  m->containing_type = containing_type;
  m->is_sorted = true;

  name = google_protobuf_DescriptorProto_name(msg_proto);
  _upb_DefBuilder_CheckIdentNotFull(ctx, name);

  m->full_name = _upb_DefBuilder_MakeFullName(ctx, prefix, name);
  _upb_DefBuilder_Add(ctx, m->full_name, _upb_DefType_Pack(m, UPB_DEFTYPE_MSG));

  oneofs = google_protobuf_DescriptorProto_oneof_decl(msg_proto, &n_oneof);
  fields = google_protobuf_DescriptorProto_field(msg_proto, &n_field);
  ext_ranges = google_protobuf_DescriptorProto_extension_range(msg_proto, &n_ext_range);

  bool ok = upb_inttable_init(&m->itof, ctx->arena);
  if (!ok) _upb_DefBuilder_OomErr(ctx);

  ok = upb_strtable_init(&m->ntof, n_oneof + n_field, ctx->arena);
  if (!ok) _upb_DefBuilder_OomErr(ctx);

  if (ctx->layout) {
    /* create_fielddef() below depends on this being set. */
    UPB_ASSERT(ctx->msg_count < ctx->layout->msg_count);
    m->layout = ctx->layout->msgs[ctx->msg_count++];
    UPB_ASSERT(n_field == m->layout->field_count);
  } else {
    /* Allocate now (to allow cross-linking), populate later. */
    m->layout = _upb_DefBuilder_Alloc(
        ctx, sizeof(*m->layout) + sizeof(_upb_FastTable_Entry));
  }

  UBP_DEF_SET_OPTIONS(m->opts, DescriptorProto, MessageOptions, msg_proto);

  m->oneof_count = n_oneof;
  m->oneofs = _upb_OneofDefs_New(ctx, n_oneof, oneofs, m);

  m->field_count = n_field;
  m->fields =
      _upb_FieldDefs_New(ctx, n_field, fields, m->full_name, m, &m->is_sorted);

  m->ext_range_count = n_ext_range;
  m->ext_ranges = _upb_ExtensionRanges_New(ctx, n_ext_range, ext_ranges, m);

  const size_t synthetic_count = _upb_OneofDefs_Finalize(ctx, m);
  m->real_oneof_count = m->oneof_count - synthetic_count;

  assign_msg_wellknowntype(m);
  upb_inttable_compact(&m->itof, ctx->arena);

  const google_protobuf_EnumDescriptorProto* const* enums =
      google_protobuf_DescriptorProto_enum_type(msg_proto, &n_enum);
  m->nested_enum_count = n_enum;
  m->nested_enums = _upb_EnumDefs_New(ctx, n_enum, enums, m);

  const google_protobuf_FieldDescriptorProto* const* exts =
      google_protobuf_DescriptorProto_extension(msg_proto, &n_ext);
  m->nested_ext_count = n_ext;
  m->nested_exts = _upb_FieldDefs_New(ctx, n_ext, exts, m->full_name, m, NULL);

  const google_protobuf_DescriptorProto* const* msgs =
      google_protobuf_DescriptorProto_nested_type(msg_proto, &n_msg);
  m->nested_msg_count = n_msg;
  m->nested_msgs = _upb_MessageDefs_New(ctx, n_msg, msgs, m);
}

// Allocate and initialize an array of |n| message defs.
upb_MessageDef* _upb_MessageDefs_New(
    upb_DefBuilder* ctx, int n, const google_protobuf_DescriptorProto* const* protos,
    const upb_MessageDef* containing_type) {
  _upb_DefType_CheckPadding(sizeof(upb_MessageDef));

  const char* name = containing_type ? containing_type->full_name
                                     : _upb_FileDef_RawPackage(ctx->file);

  upb_MessageDef* m = _upb_DefBuilder_Alloc(ctx, sizeof(upb_MessageDef) * n);
  for (int i = 0; i < n; i++) {
    create_msgdef(ctx, name, protos[i], containing_type, &m[i]);
  }
  return m;
}
