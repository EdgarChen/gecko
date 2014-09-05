/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/IccInfo.h"

#include "nsIIccInfo.h"
#include "nsPIDOMWindow.h"

#define CONVERT_STRING_TO_NULLABLE_ENUM(_string, _enumType, _enum)      \
{                                                                       \
  uint32_t i = 0;                                                       \
  for (const EnumEntry* entry = _enumType##Values::strings;             \
       entry->value;                                                    \
       ++entry, ++i) {                                                  \
    if (_string.EqualsASCII(entry->value)) {                            \
      _enum.SetValue(static_cast<_enumType>(i));                        \
    }                                                                   \
  }                                                                     \
}

using namespace mozilla::dom;

// IccInfo

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(IccInfo, mWindow)

NS_IMPL_CYCLE_COLLECTING_ADDREF(IccInfo)
NS_IMPL_CYCLE_COLLECTING_RELEASE(IccInfo)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(IccInfo)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

IccInfo::IccInfo(nsPIDOMWindow* aWindow)
  : mWindow(aWindow)
{
  SetIsDOMBinding();
}

void
IccInfo::Update(nsIIccInfo* aInfo)
{
  if (!aInfo) {
    return;
  }

  nsAutoString type;
  aInfo->GetIccType(type);
  CONVERT_STRING_TO_NULLABLE_ENUM(type, IccType, mIccType);

  aInfo->GetIccid(mIccId);
  aInfo->GetMcc(mMcc);
  aInfo->GetMnc(mMnc);
  aInfo->GetSpn(mSpn);
  aInfo->GetIsDisplayNetworkNameRequired(&mIsDisplayNetworkNameRequired);
  aInfo->GetIsDisplaySpnRequired(&mIsDisplaySpnRequired);
}

JSObject*
IccInfo::WrapObject(JSContext* aCx)
{
  return MozIccInfoBinding::Wrap(aCx, this);
}

Nullable<IccType>
IccInfo::GetIccType() const
{
  return mIccType;
}

void
IccInfo::GetIccid(nsAString& aIccId) const
{
  aIccId = mIccId;
}

void
IccInfo::GetMcc(nsAString& aMcc) const
{
  aMcc = mMcc;
}

void
IccInfo::GetMnc(nsAString& aMnc) const
{
  aMnc = mMnc;
}

void
IccInfo::GetSpn(nsAString& aSpn) const
{
  aSpn = mSpn;
}

bool
IccInfo::IsDisplayNetworkNameRequired() const
{
  return mIsDisplayNetworkNameRequired;
}

bool
IccInfo::IsDisplaySpnRequired() const
{
  return mIsDisplaySpnRequired;
}

// GsmIccInfo

NS_IMPL_CYCLE_COLLECTION_CLASS(GsmIccInfo)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(GsmIccInfo, IccInfo)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(GsmIccInfo, IccInfo)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(GsmIccInfo)
NS_INTERFACE_MAP_END_INHERITING(IccInfo)

NS_IMPL_ADDREF_INHERITED(GsmIccInfo, IccInfo)
NS_IMPL_RELEASE_INHERITED(GsmIccInfo, IccInfo)

GsmIccInfo::GsmIccInfo(nsPIDOMWindow* aWindow)
  : IccInfo(aWindow)
{
  SetIsDOMBinding();
}

void
GsmIccInfo::Update(nsIGsmIccInfo* aInfo)
{
  if (!aInfo) {
    return;
  }

  IccInfo::Update(aInfo);

  aInfo->GetMsisdn(mMsisdn);
}

JSObject*
GsmIccInfo::WrapObject(JSContext* aCx)
{
  return MozGsmIccInfoBinding::Wrap(aCx, this);
}

void
GsmIccInfo::GetMsisdn(nsAString &aMsisdn) const
{
  aMsisdn = mMsisdn;
}

// CdmaIccInfo

NS_IMPL_CYCLE_COLLECTION_CLASS(CdmaIccInfo)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(CdmaIccInfo, IccInfo)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(CdmaIccInfo, IccInfo)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(CdmaIccInfo)
NS_INTERFACE_MAP_END_INHERITING(IccInfo)

NS_IMPL_ADDREF_INHERITED(CdmaIccInfo, IccInfo)
NS_IMPL_RELEASE_INHERITED(CdmaIccInfo, IccInfo)

CdmaIccInfo::CdmaIccInfo(nsPIDOMWindow* aWindow)
  : IccInfo(aWindow)
{
  SetIsDOMBinding();
}

void
CdmaIccInfo::Update(nsICdmaIccInfo* aInfo)
{
  if (!aInfo) {
    return;
  }

  IccInfo::Update(aInfo);

  aInfo->GetMdn(mMdn);
  aInfo->GetPrlVersion(&mPrlVersion);
}

JSObject*
CdmaIccInfo::WrapObject(JSContext* aCx)
{
  return MozCdmaIccInfoBinding::Wrap(aCx, this);
}

void
CdmaIccInfo::GetMdn(nsAString& aMdn) const
{
  aMdn = mMdn;
}

int32_t
CdmaIccInfo::PrlVersion() const
{
  return mPrlVersion;
}
