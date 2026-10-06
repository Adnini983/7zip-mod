// PakHandler.cpp
// 7-Zip MOD: archive handler for PopCap .pak archives (Plants vs. Zombies and
// other PopCap games).
//
// The format (verified byte-exact against the reference implementation
// pvz-bintools / pakc):
//   * whole file is obfuscated by XORing every byte with 0xF7
//   * plaintext layout:
//       header:  magic u32 LE 0xBAC04AC0, version u32 LE 0   (8 bytes)
//       index:   0x00 + path_len(u8) + path(ascii, '\' separated)
//                + file_size(u32 LE) + FILETIME(u64 LE)     per entry
//                0x80 == end-of-index marker
//       data:    raw (uncompressed) file contents, in index order
//   * entries are ordered by the reference cmp_paths() comparator
//   * no directory entries are stored

#include "StdAfx.h"

#include "../../../C/CpuArch.h"

#include "../../Common/ComTry.h"
#include "../../Common/StringConvert.h"
#include "../../Common/UTFConvert.h"

#include "../../Windows/PropVariant.h"
#include "../../Windows/TimeUtils.h"

#include "../Common/LimitedStreams.h"
#include "../Common/ProgressUtils.h"
#include "../Common/RegisterArc.h"
#include "../Common/StreamUtils.h"

#include "../Compress/CopyCoder.h"

#include "Common/ItemNameUtils.h"

using namespace NWindows;

namespace NArchive {
namespace NPak {

static const UInt32 kMagic   = 0xBAC04AC0;
static const UInt32 kVersion = 0;
static const Byte   kXorKey  = 0xF7;
static const Byte   kCtrl_Entry = 0x00;
static const Byte   kCtrl_EOF   = 0x80;

// On-disk signature (obfuscated): magic LE ^ 0xF7, version 0 ^ 0xF7.
static const Byte k_Signature[] = { 0x37, 0xBD, 0x37, 0x4D, 0xF7, 0xF7, 0xF7, 0xF7 };

enum EErrorType
{
  k_ErrorType_OK,
  k_ErrorType_BadSignature,
  k_ErrorType_Corrupted,
  k_ErrorType_UnexpectedEnd
};

struct CItem
{
  AString Name;        // stored path, ASCII, '\' separators
  UInt64 Size;
  UInt64 Time;         // Windows FILETIME (64-bit)
  UInt64 DataOffset;
  int IndexInClient;   // write side: index for updateCallback->GetStream
  bool IsDir;          // write side

  CItem(): Size(0), Time(0), DataOffset(0), IndexInClient(-1), IsDir(false) {}
};


// ---- XOR filter streams ------------------------------------------------
// Pak obfuscation is a position-invariant per-byte XOR, so offsets map 1:1
// between the plaintext and the on-disk bytes.

// Decrypting reader: wraps IInStream and XORs every byte as it is read.
Z7_CLASS_IMP_IInStream(CXorInStream)
  CMyComPtr<IInStream> _stream;
public:
  void SetStream(IInStream *stream) { _stream = stream; }
};

Z7_COM7F_IMF(CXorInStream::Read(void *data, UInt32 size, UInt32 *processedSize))
{
  HRESULT res = _stream->Read(data, size, processedSize);
  if (processedSize)
  {
    Byte *p = (Byte *)data;
    const UInt32 n = *processedSize;
    for (UInt32 i = 0; i < n; i++)
      p[i] ^= kXorKey;
  }
  return res;
}

Z7_COM7F_IMF(CXorInStream::Seek(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition))
{
  return _stream->Seek(offset, seekOrigin, newPosition);
}

// Encrypting writer: wraps ISequentialOutStream and XORs every byte first.
Z7_CLASS_IMP_NOQIB_1(CXorOutStream, ISequentialOutStream)
  CMyComPtr<ISequentialOutStream> _stream;
  CByteBuffer _buf;
public:
  void SetStream(ISequentialOutStream *stream) { _stream = stream; }
};

Z7_COM7F_IMF(CXorOutStream::Write(const void *data, UInt32 size, UInt32 *processedSize))
{
  if (size == 0)
  {
    if (processedSize) *processedSize = 0;
    return S_OK;
  }
  if (_buf.Size() < size)
    _buf.Alloc(size);
  Byte *p = _buf;
  const Byte *src = (const Byte *)data;
    for (UInt32 i = 0; i < size; i++)
      p[i] = src[i] ^ kXorKey;
    return _stream->Write(p, size, processedSize);
}


// ---- helpers -------------------------------------------------------------

static bool IsAlnum(Byte c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static Byte ToLower(Byte c)
{
  if (c >= 'A' && c <= 'Z')
    return (Byte)(c + ('a' - 'A'));
  return c;
}

// Mirrors cmp_paths() from the reference implementation:
//   '-' < ' ' < '.' < alphanumeric (case-insensitive) < other ASCII
static int ComparePakPaths(const AString &a, const AString &b)
{
  const char *pa = a.Ptr();
  const char *pb = b.Ptr();
  const unsigned la = a.Len();
  const unsigned lb = b.Len();
  const unsigned n = (la < lb) ? la : lb;
  for (unsigned i = 0; i < n; i++)
  {
    const Byte c1 = (Byte)pa[i];
    const Byte c2 = (Byte)pb[i];
    if (c1 == c2)
      continue;
    if (c1 >= 0x80 || c2 >= 0x80)
      return (c1 < c2) ? -1 : 1;
    static const char kSpecials[] = { '-', ' ', '.' };
    for (unsigned s = 0; s < Z7_ARRAY_SIZE(kSpecials); s++)
    {
      const Byte x = (Byte)kSpecials[s];
      if (c1 == x)
      {
        if (c2 != x) return -1;
      }
      else if (c2 == x)
        return 1;
    }
    const bool a1 = IsAlnum(c1);
    const bool a2 = IsAlnum(c2);
    if (a1 && a2)
    {
      const Byte l1 = ToLower(c1);
      const Byte l2 = ToLower(c2);
      if (l1 != l2)
        return (l1 < l2) ? -1 : 1;
    }
    else if (a1 && !a2)
      return -1;
    else if (!a1 && a2)
      return 1;
    else if (c1 != c2)
      return (c1 < c2) ? -1 : 1;
  }
  return (la < lb) ? -1 : (la > lb) ? 1 : 0;
}

static int CompareItems(void *const *p1, void *const *p2, void * /* param */)
{
  const CItem &u1 = *(*((const CItem *const *)p1));
  const CItem &u2 = *(*((const CItem *const *)p2));
  return ComparePakPaths(u1.Name, u2.Name);
}


API_FUNC_static_IsArc IsArc_Pak(const Byte *p, size_t size)
{
  if (size < 8)
    return k_IsArc_Res_NEED_MORE;
  for (unsigned i = 0; i < 8; i++)
    if (p[i] != k_Signature[i])
      return k_IsArc_Res_NO;
  return k_IsArc_Res_YES;
}
}   // closes the extern "C" { from API_FUNC_static_IsArc


// ---- handler ---------------------------------------------------------------

Z7_CLASS_IMP_CHandler_IInArchive_3(
    IInArchiveGetStream
  , ISetProperties
  , IOutArchive
)
public:
  CObjectVector<CItem> _items;
  CMyComPtr<IInStream> _stream;
  CMyComPtr2_Create<IInStream, CXorInStream> _xorStream;
  UInt64 _phySize;
  bool _isArc;
  EErrorType _error;

  HRESULT ReadExact(Byte *data, size_t size)
  {
    size_t processed = size;
    const HRESULT res = ReadStream(_xorStream.Interface(), data, &processed);
    if (processed != size)
      _error = k_ErrorType_UnexpectedEnd;
    return res;
  }
};


static const Byte kProps[] =
{
  kpidPath,
  kpidIsDir,
  kpidSize,
  kpidPackSize,
  kpidMTime
};

IMP_IInArchive_Props
IMP_IInArchive_ArcProps_NO_Table


Z7_COM7F_IMF(CHandler::GetArchiveProperty(PROPID propID, PROPVARIANT *value))
{
  COM_TRY_BEGIN
  NCOM::CPropVariant prop;
  switch (propID)
  {
    case kpidPhySize: prop = _phySize; break;
    case kpidErrorFlags:
    {
      UInt32 v = 0;
      if (!_isArc)
        v |= kpv_ErrorFlags_IsNotArc;
      switch (_error)
      {
        case k_ErrorType_UnexpectedEnd: v |= kpv_ErrorFlags_UnexpectedEnd; break;
        case k_ErrorType_Corrupted:     v |= kpv_ErrorFlags_HeadersError; break;
        case k_ErrorType_OK:
        case k_ErrorType_BadSignature:
          break;
      }
      prop = v;
      break;
    }
  }
  prop.Detach(value);
  return S_OK;
  COM_TRY_END
}


Z7_COM7F_IMF(CHandler::Open(IInStream *stream, const UInt64 * /* maxCheckStartPos */,
    IArchiveOpenCallback * /* callback */))
{
  COM_TRY_BEGIN
  {
    Close();

    UInt64 endPos;
    RINOK(InStream_AtBegin_GetSize(stream, endPos))

    _stream = stream;
    _xorStream->SetStream(stream);

    Byte hdr[8];
    RINOK(ReadExact(hdr, 8))
    if (_error != k_ErrorType_OK)
      return S_OK;
    if (GetUi32(hdr) != kMagic || GetUi32(hdr + 4) != kVersion)
    {
      _error = k_ErrorType_BadSignature;
      return S_FALSE;
    }

    UInt64 dataStart = 8;
    for (;;)
    {
      Byte ctrl;
      RINOK(ReadExact(&ctrl, 1))
      if (_error != k_ErrorType_OK)
        return S_OK;
      dataStart++;
      if (ctrl == kCtrl_EOF)
        break;
      if (ctrl != kCtrl_Entry)
      {
        _error = k_ErrorType_Corrupted;
        return S_OK;
      }

      Byte lenB;
      RINOK(ReadExact(&lenB, 1))
      if (_error != k_ErrorType_OK)
        return S_OK;
      dataStart++;
      if (lenB == 0 || lenB >= 0xFF)
      {
        _error = k_ErrorType_Corrupted;
        return S_OK;
      }

      CItem item;
      {
        char buf[0xFF];
        RINOK(ReadExact((Byte *)buf, lenB))
        if (_error != k_ErrorType_OK)
          return S_OK;
        for (unsigned i = 0; i < lenB; i++)
        {
          const Byte c = (Byte)buf[i];
          if (c == 0 || c >= 0x80)
          {
            _error = k_ErrorType_Corrupted;
            return S_OK;
          }
        }
        item.Name.SetFrom_CalcLen(buf, lenB);
      }
      dataStart += lenB;

      Byte meta[12]; // size(4) + FILETIME(8)
      RINOK(ReadExact(meta, 12))
      if (_error != k_ErrorType_OK)
        return S_OK;
      dataStart += 12;
      item.Size = GetUi32(meta);
      item.Time = (UInt64)GetUi32(meta + 4) | ((UInt64)GetUi32(meta + 8) << 32);

      _items.Add(item);
    }

    UInt64 pos = dataStart;
    for (unsigned i = 0; i < _items.Size(); i++)
    {
      _items[i].DataOffset = pos;
      pos += _items[i].Size;
    }
    _phySize = pos;
    if (_phySize > endPos)
    {
      _error = k_ErrorType_UnexpectedEnd;
      return S_OK;
    }
  }
  _isArc = true;
  return S_OK;
  COM_TRY_END
}


Z7_COM7F_IMF(CHandler::Close())
{
  _items.Clear();
  _stream.Release();
  _phySize = 0;
  _isArc = false;
  _error = k_ErrorType_OK;
  return S_OK;
}


Z7_COM7F_IMF(CHandler::GetNumberOfItems(UInt32 *numItems))
{
  *numItems = _items.Size();
  return S_OK;
}


Z7_COM7F_IMF(CHandler::GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value))
{
  COM_TRY_BEGIN
  NCOM::CPropVariant prop;
  const CItem &item = _items[index];

  switch (propID)
  {
    case kpidPath:
    {
      // pak stores '\' separators; 7-Zip uses '/' as the canonical separator.
      const unsigned len = item.Name.Len();
      char tmp[0xFF];
      for (unsigned i = 0; i < len; i++)
        tmp[i] = (item.Name[i] == '\\') ? '/' : item.Name[i];
      AString name;
      name.SetFrom_CalcLen(tmp, len);
      UString u;
      ConvertUTF8ToUnicode(name, u);
      prop = NItemName::GetOsPath(u);
      break;
    }
    case kpidIsDir: prop = false; break;
    case kpidSize: prop = item.Size; break;
    case kpidPackSize: prop = item.Size; break;
    case kpidMTime:
    {
      FILETIME ft;
      ft.dwLowDateTime = (DWORD)(item.Time & 0xFFFFFFFF);
      ft.dwHighDateTime = (DWORD)(item.Time >> 32);
      prop = ft;
      break;
    }
  }
  prop.Detach(value);
  return S_OK;
  COM_TRY_END
}


Z7_COM7F_IMF(CHandler::Extract(const UInt32 *indices, UInt32 numItems,
    Int32 testMode, IArchiveExtractCallback *extractCallback))
{
  COM_TRY_BEGIN
  const bool allFilesMode = (numItems == (UInt32)(Int32)-1);
  if (allFilesMode)
    numItems = _items.Size();
  if (numItems == 0)
    return S_OK;

  UInt64 totalSize = 0;
  for (UInt32 i = 0; i < numItems; i++)
  {
    const UInt32 index = allFilesMode ? i : indices[i];
    totalSize += _items[index].Size;
  }
  RINOK(extractCallback->SetTotal(totalSize))

  CMyComPtr2_Create<ICompressCoder, NCompress::CCopyCoder> copyCoder;
  CMyComPtr2_Create<ICompressProgressInfo, CLocalProgress> lps;
  lps->Init(extractCallback, false);
  CMyComPtr2_Create<ISequentialInStream, CLimitedSequentialInStream> inStream;
  inStream->SetStream(_xorStream.Interface());

  UInt64 total_PackSize = 0;
  UInt64 total_UnpackSize = 0;

  for (UInt32 i = 0;; i++)
  {
    lps->InSize = total_PackSize;
    lps->OutSize = total_UnpackSize;
    RINOK(lps->SetCur())
    if (i >= numItems)
      break;
    const Int32 askMode = testMode ?
        NExtract::NAskMode::kTest :
        NExtract::NAskMode::kExtract;
    const UInt32 index = allFilesMode ? i : indices[i];
    const CItem &item = _items[index];

    CMyComPtr<ISequentialOutStream> outStream;
    RINOK(extractCallback->GetStream(index, &outStream, askMode))

    total_PackSize += item.Size;
    total_UnpackSize += item.Size;

    if (!testMode && !outStream)
      continue;
    RINOK(extractCallback->PrepareOperation(askMode))

    RINOK(InStream_SeekSet(_xorStream.Interface(), item.DataOffset))
    inStream->Init(item.Size);
    RINOK(copyCoder.Interface()->Code(inStream, outStream, NULL, NULL, lps))
    Int32 res = NExtract::NOperationResult::kDataError;
    if (copyCoder->TotalSize == item.Size)
      res = NExtract::NOperationResult::kOK;
    RINOK(extractCallback->SetOperationResult(res))
  }
  return S_OK;
  COM_TRY_END
}


Z7_COM7F_IMF(CHandler::GetStream(UInt32 index, ISequentialInStream **stream))
{
  COM_TRY_BEGIN
  const CItem &item = _items[index];
  return CreateLimitedInStream(_xorStream.Interface(), item.DataOffset, item.Size, stream);
  COM_TRY_END
}

Z7_COM7F_IMF(CHandler::GetFileTimeType(UInt32 *type))
{
  *type = NFileTimeType::kWindows;   // pak stores Windows FILETIME values
  return S_OK;
}


// Pak is a store-only format: every compression parameter is ignored.
Z7_COM7F_IMF(CHandler::SetProperties(const wchar_t * const * /* names */,
    const PROPVARIANT * /* values */, UInt32 /* numProps */))
{
  return S_OK;
}


#ifndef Z7_EXTRACT_ONLY

Z7_COM7F_IMF(CHandler::UpdateItems(ISequentialOutStream *outStream, UInt32 numItems,
    IArchiveUpdateCallback *callback))
{
  COM_TRY_BEGIN
  if (!callback)
    return E_INVALIDARG;

  CObjectVector<CItem> items;

  for (UInt32 i = 0; i < numItems; i++)
  {
    Int32 newData;
    Int32 newProps;
    UInt32 indexInArc;
    RINOK(callback->GetUpdateItemInfo(i, &newData, &newProps, &indexInArc))
    if (!IntToBool(newData))
      continue;          // pak only stores newly added files
    {
      NCOM::CPropVariant prop;
      RINOK(callback->GetProperty(i, kpidIsDir, &prop))
      if (prop.vt == VT_BOOL && prop.boolVal != VARIANT_FALSE)
        continue;        // pak stores no directory entries
    }

    CItem item;
    {
      NCOM::CPropVariant prop;
      RINOK(callback->GetProperty(i, kpidPath, &prop))
      if (prop.vt != VT_BSTR)
        return E_INVALIDARG;
      UString s = prop.bstrVal;
      for (unsigned k = 0; k < s.Len(); k++)
      {
        wchar_t c = s[k];
        if (c == L'/')
          c = L'\\';
        if (c >= 0x80)
          return E_INVALIDARG;   // pak paths are ASCII-only
        item.Name += (char)c;
      }
    }
    if (item.Name.IsEmpty() || item.Name.Len() >= 0xFF)
      return E_INVALIDARG;

    {
      NCOM::CPropVariant prop;
      RINOK(callback->GetProperty(i, kpidSize, &prop))
      if (prop.vt != VT_UI8)
        return E_INVALIDARG;
      item.Size = prop.uhVal.QuadPart;
      if (item.Size >= ((UInt64)1 << 32))
        return E_INVALIDARG;     // pak stores 32-bit sizes
    }

    {
      NCOM::CPropVariant prop;
      RINOK(callback->GetProperty(i, kpidMTime, &prop))
      if (prop.vt == VT_FILETIME)
        item.Time = ((UInt64)prop.filetime.dwHighDateTime << 32) | prop.filetime.dwLowDateTime;
      else if (prop.vt != VT_EMPTY)
        return E_INVALIDARG;
    }

    item.IndexInClient = (int)i;
    items.Add(item);
  }

  // Sort in the reference cmp_paths order.
  if (items.Size() >= 2)
    items.Sort(CompareItems, NULL);

  // Build plaintext index (header + entries + EOF).
  UInt64 indexSize = 8 + 1;
  for (unsigned k = 0; k < items.Size(); k++)
    indexSize += 1 + 1 + items[k].Name.Len() + 4 + 8;

  CByteBuffer buf;
  buf.Alloc((size_t)indexSize);
  {
    Byte *p = buf;
    SetUi32(p, kMagic);
    SetUi32(p + 4, kVersion);
    p += 8;
    for (unsigned k = 0; k < items.Size(); k++)
    {
      const CItem &item = items[k];
      *p++ = kCtrl_Entry;
      *p++ = (Byte)item.Name.Len();
      memcpy(p, item.Name.Ptr(), item.Name.Len());
      p += item.Name.Len();
      SetUi32(p, (UInt32)item.Size);
      p += 4;
      SetUi32(p, (UInt32)(item.Time & 0xFFFFFFFF));
      p += 4;
      SetUi32(p, (UInt32)(item.Time >> 32));
      p += 4;
    }
    *p++ = kCtrl_EOF;
  }
  // Encrypt the index in place.
  {
    Byte *p = buf;
    for (UInt64 k = 0; k < indexSize; k++)
      p[k] ^= kXorKey;
  }

  {
    UInt64 total = indexSize;
    for (unsigned k = 0; k < items.Size(); k++)
      total += items[k].Size;
    callback->SetTotal(total);
  }

  RINOK(WriteStream(outStream, buf, (size_t)indexSize))

  // Write data in sorted order, encrypted on the fly.
  CMyComPtr2_Create<ISequentialOutStream, CXorOutStream> xorOut;
  xorOut->SetStream(outStream);
  CMyComPtr2_Create<ICompressCoder, NCompress::CCopyCoder> copyCoder;

  UInt64 done = indexSize;
  for (unsigned k = 0; k < items.Size(); k++)
  {
    const CItem &item = items[k];
    CMyComPtr<ISequentialInStream> fileInStream;
    const HRESULT res = callback->GetStream((UInt32)item.IndexInClient, &fileInStream);
    if (res == S_FALSE)
      continue;
    RINOK(res)
    if (!fileInStream)
      continue;
    RINOK(copyCoder.Interface()->Code(fileInStream, xorOut.Interface(), NULL, NULL, NULL))
    done += item.Size;
    RINOK(callback->SetCompleted(&done))
    RINOK(callback->SetOperationResult(NArchive::NUpdate::NOperationResult::kOK))
  }

  return S_OK;
  COM_TRY_END
}

#endif // !Z7_EXTRACT_ONLY


REGISTER_ARC_IO(
  "pak", "pak", NULL, 0xE8,
  k_Signature,
  0,
  0,
  0,
  IsArc_Pak)

}}
