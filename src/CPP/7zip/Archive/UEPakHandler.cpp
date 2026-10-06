// UEPakHandler.cpp
// 7-Zip MOD - Unreal Engine PAK handler (external plugin DLL: 7zUEPak.dll).
//
// Read side: footer/PakInfo parsing + automatic Pak-Version detection +
// index parsing (v2..v9 simple index) + Store extraction. Format facts follow
// repak (MIT OR Apache-2.0, black-box reference only; independent C++ impl).
//
// File layout (repak footer.rs / pak.rs / entry.rs / ext.rs):
//   [file data: FPakEntry(offset=0)+content per file]
//   [index: MountPoint FString, NumEntries u32, then per file: path FString
//           + FPakEntry(absolute offset)]
//   [FPakInfo at very end, or preceded by 5-byte footer
//    [FooterSize u8][FooterMagic u32 = 0x5A6F12E1]]
//   FPakInfo (repak order): EncryptionKeyGuid(u128,v7+) bEncryptedIndex(u8,v4+)
//     Magic(u32) VersionMajor(u32) IndexOffset(u64) IndexSize(u64)
//     IndexHash(20) bIndexIsFrozen(u8,v9) CompressionNames(v8A+:4, v8B+:5)
//   FPakEntry: Offset(u64) Compressed(u64) Uncompressed(u64)
//     Compression(v8A:u8 else u32; 0=Store else slot+1) [Timestamp(u64,v1)]
//     Hash(20) [v3+: Blocks(u32 count + 16*count) Flags(u8) BlockSize(u32)]

#include "StdAfx.h"

#include "../../Common/MyCom.h"
#include "../../Common/MyBuffer.h"
#include "../../Common/ComTry.h"
#include "../Common/StreamUtils.h"
#include "../Common/StreamObjects.h"
#include "../Compress/ZlibEncoder.h"
#include "../Compress/ZlibDecoder.h"

#include "../../../C/CpuArch.h"
#include "../../../C/Sha1.h"

#include "IArchive.h"

#include "../../Windows/PropVariant.h"

#include "../Common/RegisterArc.h"

#include "../../Common/StringConvert.h"

using namespace NWindows;

#include <stdio.h>
#define UEDBG(...) ((void)0)   /* flip to fprintf(stderr,__VA_ARGS__) for tracing */

namespace NArchive {
namespace NUEPak {

static const UInt32 kFooterMagic = 0x5A6F12E1;

// V8A uses a 1-byte compression slot; every other version (incl. 8B, 9+) uses u32.
static bool IsV8A(UInt32 version, bool is8b) { return version == 8 && !is8b; }

// ---- in-memory little-endian reader over a byte buffer ----
class CBufReader
{
  const Byte *_p;
  size_t _size;
  size_t _pos;
public:
  CBufReader(const Byte *p, size_t size): _p(p), _size(size), _pos(0) {}
  bool ReadBytes(void *dst, size_t n)
  {
    if (_pos + n > _size) return false;
    memcpy(dst, _p + _pos, n);
    _pos += n;
    return true;
  }
  bool ReadU8(UInt32 &v) { Byte b; if (!ReadBytes(&b, 1)) return false; v = b; return true; }
  bool ReadU32(UInt32 &v) { if (!ReadBytes(&v, 4)) return false; return true; }
  bool ReadU64(UInt64 &v) { if (!ReadBytes(&v, 8)) return false; return true; }
  bool Skip(size_t n) { if (_pos + n > _size) return false; _pos += n; return true; }
  // FString: i32 len; <0 -> UTF-16 (|len| u16 incl. terminator), else bytes (len incl. terminator)
  bool ReadFString(UString &s)
  {
    Int32 len;
    if (!ReadBytes(&len, 4)) return false;
    if (len < 0)
    {
      UInt32 n = (UInt32)(-len);
      if ((size_t)n * 2 > _size - _pos) return false;
      UString tmp;
      for (UInt32 i = 0; i < n; i++)
      {
        UInt16 c; memcpy(&c, _p + _pos, 2); _pos += 2;
        if (c != 0) tmp += (wchar_t)c;
        else break;
      }
      s = tmp;
      return true;
    }
    if ((size_t)len > _size - _pos) return false;
    const char *cs = (const char *)(_p + _pos);
    size_t length = 0;
    while (length < (size_t)len && cs[length] != 0) length++;
    {
      UString tmp;
      for (size_t i = 0; i < length; i++)
        tmp += (wchar_t)(Byte)cs[i];
      s = tmp;
    }
    _pos += (size_t)len;
    return true;
  }
};

// ---- a parsed file entry ----
struct CBlock
{
  UInt64 Start;   // offset of compressed block relative to entry start
  UInt64 End;
};

struct CEntry
{
  UString Name;          // index path
  UInt64 Offset;         // absolute offset of the data-area FPakEntry
  UInt64 Size;           // compressed / on-disk size
  UInt64 Uncompressed;
  UInt32 Compression;    // 0 = store; else slot index into compression list
  bool Encrypted;
  UInt32 NumBlocks;      // 0 for store entries
  CObjectVector<CBlock> Blocks;   // zlib/gzip block table (relative to entry start)
  UInt32 BlockSize;      // compression block size (v3+)
};

struct CPakInfo
{
  UInt32 Version;        // Pak major version (2..11)
  bool Is8B;
  UInt64 IndexOffset;
  UInt64 IndexSize;
  bool Encrypted;
  bool Frozen;
  UInt32 NumCompression;
  UString CompressionNames[5];
};

static UInt64 PakInfoSize(UInt32 version, bool is8b)
{
  UInt64 s = 4 + 4 + 8 + 8 + 20;   // magic, version, offset, size, hash
  if (version >= 7) s += 16;       // encryption key guid
  if (version >= 4) s += 1;        // bEncryptedIndex
  if (version == 9) s += 1;        // bIndexIsFrozen
  if (version >= 8) s += (is8b ? 5 : 4) * 32;   // compression names
  return s;
}

// Parse an FPakInfo buffer of a given candidate version; validate magic.
// Returns true and fills info on success.
static bool ParsePakInfo(const Byte *buf, size_t size, UInt32 version, bool is8b, CPakInfo &info)
{
  if (size != PakInfoSize(version, is8b))
    return false;
  CBufReader r(buf, size);
  if (version >= 7)
  {
    if (!r.Skip(16)) return false;   // encryption key guid
  }
  if (version >= 4)
  {
    UInt32 enc;
    if (!r.ReadU8(enc)) return false;
    info.Encrypted = (enc != 0);
  }
  else
    info.Encrypted = false;
  UInt32 magic, major;
  if (!r.ReadU32(magic) || magic != kFooterMagic) return false;
  if (!r.ReadU32(major)) return false;
  if (major != version) return false;
  if (!r.ReadU64(info.IndexOffset)) return false;
  if (!r.ReadU64(info.IndexSize)) return false;
  if (!r.Skip(20)) return false;     // index hash
  if (version == 9)
  {
    UInt32 frozen;
    if (!r.ReadU8(frozen)) return false;
    info.Frozen = (frozen != 0);
  }
  else
    info.Frozen = false;
  info.Version = version;
  info.Is8B = is8b;
  info.NumCompression = (version >= 8) ? (is8b ? 5 : 4) : 0;
  for (UInt32 i = 0; i < 5; i++)
    info.CompressionNames[i].Empty();
  for (UInt32 i = 0; i < info.NumCompression; i++)
  {
    char name[32];
    if (!r.ReadBytes(name, 32)) return false;
    size_t len = 0;
    while (len < 32 && name[len] != 0) len++;
    // UE compression names are ASCII; convert byte-by-byte (the length must
    // not be passed as a code page to MultiByteToUnicodeString).
    UString n;
    for (size_t j = 0; j < len; j++)
      n += (wchar_t)(Byte)name[j];
    info.CompressionNames[i] = n;
  }
  return true;
}

// FPakEntry serialized size when stored (no compression blocks).
static UInt64 StoredDataEntrySize(UInt32 version, bool is8b)
{
  UInt64 s = 8 + 8 + 8 + (IsV8A(version, is8b) ? 1 : 4) + 20;   // offset,comp,uncomp,compression,hash
  if (version >= 3) s += 1 + 4;                  // flags + compression block size
  return s;
}

Z7_CLASS_IMP_CHandler_IInArchive_3(
    IInArchiveGetStream
  , ISetProperties
  , IOutArchive
)
  CMyComPtr<IInStream> _stream;
  UInt64 _fileSize;
  bool _opened;
  CPakInfo _info;
  CObjectVector<CEntry> _entries;
  UInt32 _writeVersion;   // target Pak version for UpdateItems (default v9, store-only)
  bool _useZlib;          // "zlib" in method parameters -> zlib-block-compress (v8+)
public:
  CHandler(): _fileSize(0), _opened(false), _writeVersion(9), _useZlib(false)
  {
    _info.Version = 0;
    _info.Is8B = false;
    _info.IndexOffset = 0;
    _info.IndexSize = 0;
    _info.Encrypted = false;
    _info.Frozen = false;
    _info.NumCompression = 0;
  }
};

static bool DetectVersion(IInStream *stream, UInt64 fileSize, CPakInfo &info)
{
  // --- footer-marker model: last 5 bytes = [FooterSize u8][FooterMagic u32] ---
  {
    Byte footer[5];
    UInt32 processed = 0;
    if (stream->Seek((Int64)fileSize - 5, STREAM_SEEK_SET, NULL) == S_OK &&
        stream->Read(footer, 5, &processed) == S_OK && processed == 5 &&
        GetUi32(footer + 1) == kFooterMagic)
    {
      UInt32 fs = footer[0];
      for (UInt32 ver = 2; ver <= 11; ver++)
      {
        for (UInt32 k = 0; k < 2; k++)
        {
          bool is8b = (ver == 8) ? (k != 0) : (ver >= 8);
          if (PakInfoSize(ver, is8b) != fs)
            continue;
          if (fs > 512) continue;
          Byte buf[512];
          if (stream->Seek((Int64)(fileSize - fs - 5), STREAM_SEEK_SET, NULL) != S_OK)
            continue;
          UInt32 proc = 0;
          if (stream->Read(buf, (UInt32)fs, &proc) != S_OK || proc != fs)
            continue;
          CPakInfo tmp;
          if (ParsePakInfo(buf, (size_t)fs, ver, is8b, tmp))
          {
            info = tmp;
            return true;
          }
        }
      }
    }
  }
  // --- end-scan model (repak): FPakInfo at End(-PakInfoSize), version loop ---
  {
    for (UInt32 ver = 11; ver >= 2; ver--)
    {
      for (UInt32 k = 0; k < 2; k++)
      {
        bool is8b = (ver == 8) ? (k != 0) : (ver >= 8);
        UInt64 sz = PakInfoSize(ver, is8b);
        if (sz > fileSize || sz > 512) continue;
        Byte buf[512];
        if (stream->Seek((Int64)(fileSize - sz), STREAM_SEEK_SET, NULL) != S_OK)
          continue;
        UInt32 proc = 0;
        if (stream->Read(buf, (UInt32)sz, &proc) != S_OK || proc != sz)
          continue;
        CPakInfo tmp;
        if (ParsePakInfo(buf, (size_t)sz, ver, is8b, tmp))
        {
          info = tmp;
          return true;
        }
      }
    }
  }
  return false;
}

// Parse one FPakEntry (index format) from a buffer reader.
static bool ParseEntry(CBufReader &r, UInt32 version, bool is8b, CEntry &e)
{
  e.Encrypted = false;
  e.Compression = 0;
  e.NumBlocks = 0;
  e.Blocks.Clear();
  e.BlockSize = 0;
  if (!r.ReadU64(e.Offset)) return false;
  if (!r.ReadU64(e.Size)) return false;
  if (!r.ReadU64(e.Uncompressed)) return false;
  UInt32 comp;
  if (IsV8A(version, is8b))
  {
    if (!r.ReadU8(comp)) return false;
  }
  else
  {
    if (!r.ReadU32(comp)) return false;
  }
  e.Compression = (comp == 0) ? 0 : (comp - 1);
  if (version == 1)
  {
    if (!r.Skip(8)) return false;   // timestamp
  }
  if (!r.Skip(20)) return false;    // hash
  if (version >= 3)
  {
    if (e.Compression != 0)
    {
      UInt32 count;
      if (!r.ReadU32(count)) return false;
      if (count > 10000000) return false;   // sanity guard
      e.NumBlocks = count;
      for (UInt32 i = 0; i < count; i++)
      {
        CBlock b;
        if (!r.ReadU64(b.Start)) return false;
        if (!r.ReadU64(b.End)) return false;
        e.Blocks.Add(b);
      }
    }
    UInt32 flags;
    if (!r.ReadU8(flags)) return false;
    e.Encrypted = (flags & 1) != 0;
    if (!r.ReadU32(e.BlockSize)) return false;   // compression block size
  }
  return true;
}

// Load the simple index (v2..v9) at _info.IndexOffset.
static bool LoadSimpleIndex(IInStream *stream, const CPakInfo &info, CObjectVector<CEntry> &entries)
{
  if (info.IndexSize > (1ULL << 31))
    return false;
  UEDBG("  LSI alloc %llu", (unsigned long long)info.IndexSize);
  CByteBuffer buf((size_t)info.IndexSize);
  if (!buf)
    return false;
  UEDBG("  LSI seek %llu", (unsigned long long)info.IndexOffset);
  if (stream->Seek((Int64)info.IndexOffset, STREAM_SEEK_SET, NULL) != S_OK)
    return false;
  UInt32 proc = 0;
  UEDBG("  LSI read");
  if (stream->Read((Byte *)buf, (UInt32)info.IndexSize, &proc) != S_OK || proc != info.IndexSize)
    return false;
  UEDBG("  LSI bufread %u", (unsigned)proc);
  CBufReader r((Byte *)buf, (size_t)info.IndexSize);
  UEDBG("  LSI ctor ok");
  UString mountPoint;
  if (!r.ReadFString(mountPoint)) return false;
  UEDBG("  LSI mount done len=%u", (unsigned)mountPoint.Len());
  UInt32 count;
  if (!r.ReadU32(count)) return false;
  UEDBG("  LSI count %u", (unsigned)count);
  for (UInt32 i = 0; i < count; i++)
  {
    CEntry e;
    if (!r.ReadFString(e.Name)) return false;
    if (!ParseEntry(r, info.Version, info.Is8B, e)) return false;
    entries.Add(e);
    UEDBG("  LSI entry %u '%ls' off=%llu size=%llu", (unsigned)i, (const wchar_t *)e.Name,
        (unsigned long long)e.Offset, (unsigned long long)e.Size);
  }
  return true;
}

Z7_COM7F_IMF(CHandler::Open(IInStream *stream, const UInt64 * /*maxCheckStartPosition*/, IArchiveOpenCallback * /*openCallback*/))
{
  if (_opened)
    return S_FALSE;
  _stream = stream;
  _entries.Clear();
  _opened = false;
  _fileSize = 0;

  if (stream->Seek(0, STREAM_SEEK_END, &_fileSize) != S_OK)
    return S_FALSE;
  if (_fileSize < 50)
    return S_FALSE;

  CPakInfo info;
  UEDBG("Open fileSize=%llu", (unsigned long long)_fileSize);
  if (!DetectVersion(stream, _fileSize, info))
  {
    UEDBG("DetectVersion FAIL");
    return S_FALSE;   // not a (supported) UE PAK - let another handler try
  }
  UEDBG("DetectVersion OK ver=%u is8b=%d off=%llu sz=%llu", info.Version, (int)info.Is8B,
      (unsigned long long)info.IndexOffset, (unsigned long long)info.IndexSize);
  if (info.Version < 2 || info.Version > 11)
    return S_FALSE;

  // v10/v11 use a different (secondary) index layout - not implemented yet.
  if (info.Version >= 10)
    return S_FALSE;

  if (!LoadSimpleIndex(stream, info, _entries))
  {
    UEDBG("LoadSimpleIndex FAIL");
    return S_FALSE;
  }
  UEDBG("LoadSimpleIndex OK entries=%u", (unsigned)_entries.Size());

  _info = info;
  _opened = true;
  return S_OK;
}

Z7_COM7F_IMF(CHandler::Close())
{
  _opened = false;
  _entries.Clear();
  _stream.Release();
  return S_OK;
}

Z7_COM7F_IMF(CHandler::GetNumberOfItems(UInt32 *numItems))
{
  *numItems = (UInt32)_entries.Size();
  return S_OK;
}

static const Byte kProps[] =
{
  kpidPath,
  kpidName,
  kpidSize,
  kpidPackSize,
  kpidMethod,
  kpidEncrypted
};

Z7_COM7F_IMF(CHandler::GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value))
{
  NWindows::NCOM::PropVariant_Clear(value);
  if (index >= (UInt32)_entries.Size())
    return E_INVALIDARG;
  const CEntry &e = _entries[index];
  switch (propID)
  {
    case kpidPath:
      value->vt = VT_BSTR;
      value->bstrVal = (BSTR)::SysAllocString((const wchar_t *)e.Name);
      break;
    case kpidName:
    {
      int slash = e.Name.ReverseFind(L'/');
      UString name = (slash >= 0) ? e.Name.Ptr(slash + 1) : e.Name;
      value->vt = VT_BSTR;
      value->bstrVal = (BSTR)::SysAllocString((const wchar_t *)name);
      break;
    }
    case kpidSize: value->vt = VT_UI8; value->uhVal.QuadPart = e.Uncompressed; break;
    case kpidPackSize: value->vt = VT_UI8; value->uhVal.QuadPart = e.Size; break;
    case kpidMethod:
    {
      UString m = (e.Compression == 0) ? UString(L"Store") :
          (e.Compression <= _info.NumCompression ? _info.CompressionNames[e.Compression] : UString(L"?"));
      value->vt = VT_BSTR;
      value->bstrVal = (BSTR)::SysAllocString((const wchar_t *)m);
      break;
    }
    case kpidEncrypted: value->vt = VT_BOOL; value->boolVal = e.Encrypted ? VARIANT_TRUE : VARIANT_FALSE; break;
    default: value->vt = VT_EMPTY; break;
  }
  return S_OK;
}

static const Byte kArcProps[] =
{
  kpidNumSubFiles,
  kpidSubType,
  kpidComment,
  kpidEncrypted
};

Z7_COM7F_IMF(CHandler::GetNumberOfArchiveProperties(UInt32 *numProps))
{
  *numProps = 4;
  return S_OK;
}

Z7_COM7F_IMF(CHandler::GetArchiveProperty(PROPID propID, PROPVARIANT *value))
{
  NWindows::NCOM::PropVariant_Clear(value);
  switch (propID)
  {
    case kpidNumSubFiles: value->vt = VT_UI4; value->ulVal = (UInt32)_entries.Size(); break;
    case kpidSubType:
      value->vt = VT_BSTR;
      value->bstrVal = (BSTR)::SysAllocString((const wchar_t *)UString(L"pak(UE4)"));
      break;
    case kpidComment:
    {
      UString s;
      s += L"Pak Version: ";
      s.Add_UInt32(_info.Version);
      if (_info.Is8B) s += L" (8B)";
      s += L"\r\nFiles: ";
      s.Add_UInt32((UInt32)_entries.Size());
      if (_info.Encrypted) s += L"\r\nEncrypted index";
      if (_info.Frozen) s += L"\r\nFrozen index";
      if (_info.NumCompression)
      {
        s += L"\r\nCompression: ";
        for (UInt32 i = 0; i < _info.NumCompression; i++)
        {
          if (i) s += L", ";
          s += _info.CompressionNames[i].IsEmpty() ? UString(L"?") : _info.CompressionNames[i];
        }
      }
      value->vt = VT_BSTR;
      value->bstrVal = (BSTR)::SysAllocString((const wchar_t *)s);
      break;
    }
    case kpidEncrypted: value->vt = VT_BOOL; value->boolVal = _info.Encrypted ? VARIANT_TRUE : VARIANT_FALSE; break;
    default: value->vt = VT_EMPTY; break;
  }
  return S_OK;
}

Z7_COM7F_IMF(CHandler::GetPropertyInfo(UInt32 index, BSTR *name, PROPID *propID, VARTYPE *varType))
{
  if (index >= 6) return E_INVALIDARG;
  *propID = kProps[index];
  *varType = (VARTYPE)VT_EMPTY;
  *name = 0;
  return S_OK;
}

Z7_COM7F_IMF(CHandler::GetArchivePropertyInfo(UInt32 index, BSTR *name, PROPID *propID, VARTYPE *varType))
{
  if (index >= 4) return E_INVALIDARG;
  *propID = kArcProps[index];
  *varType = (VARTYPE)VT_EMPTY;
  *name = 0;
  return S_OK;
}

Z7_COM7F_IMF(CHandler::GetNumberOfProperties(UInt32 *numProps))
{
  *numProps = 6;
  return S_OK;
}

// ---- read side zlib support ----

// Returns true if entry e (v8+) uses Zlib compression (by compression-name slot).
static bool IsZlibEntry(const CEntry &e, const CPakInfo &info)
{
  if (e.Compression == 0)
    return false;
  if (info.NumCompression == 0)
    return false;   // v2-v7 have no compression-name table
  if (e.Compression > info.NumCompression)
    return false;
  return (info.CompressionNames[e.Compression] == L"Zlib");
}

// Decompress one zlib block at absolute stream position `pos` into outStream.
static HRESULT DecodeZlibBlock(IInStream *stream, UInt64 pos, UInt64 compSize,
    UInt64 uncompSize, ISequentialOutStream *outStream)
{
  CByteBuffer inBuf((size_t)compSize);
  if (!inBuf)
    return E_OUTOFMEMORY;
  if (stream->Seek((Int64)pos, STREAM_SEEK_SET, NULL) != S_OK)
    return S_FALSE;
  UInt32 proc = 0;
  if (stream->Read((Byte *)inBuf, (UInt32)compSize, &proc) != S_OK || proc != compSize)
    return S_FALSE;
  CMyComPtr<ISequentialInStream> inStream;
  Create_BufInStream_WithNewBuffer((const void *)(const Byte *)inBuf, (size_t)compSize, &inStream);
  CMyComPtr<ICompressCoder> dec = new NCompress::NZlib::CDecoder;
  return dec->Code(inStream, outStream, &compSize, &uncompSize, NULL);
}

Z7_COM7F_IMF(CHandler::Extract(const UInt32 *indices, UInt32 numItems, Int32 testMode, IArchiveExtractCallback *extractCallback))
{
  UInt64 total = 0;
  for (UInt32 i = 0; i < numItems; i++)
  {
    UInt32 idx = indices[i];
    if (idx >= (UInt32)_entries.Size()) return E_INVALIDARG;
    total += _entries[idx].Size;
  }
  RINOK(extractCallback->SetTotal(total));

  UInt64 cur = 0;
  for (UInt32 i = 0; i < numItems; i++)
  {
    UInt32 idx = indices[i];
    const CEntry &e = _entries[idx];
    RINOK(extractCallback->SetCompleted(&cur));
    CMyComPtr<ISequentialOutStream> outStream;
    RINOK(extractCallback->GetStream(idx, &outStream, testMode));
    if (outStream)
    {
      if (e.Encrypted)
      {
        // encrypted not yet implemented.
        extractCallback->SetOperationResult(NArchive::NExtract::NOperationResult::kUnsupportedMethod);
        cur += e.Size;
        continue;
      }
      if (e.Compression != 0)
      {
        if (!IsZlibEntry(e, _info))
        {
          // non-zlib compressed method (gzip/oodle/zstd/...) not implemented.
          extractCallback->SetOperationResult(NArchive::NExtract::NOperationResult::kUnsupportedMethod);
          cur += e.Size;
          continue;
        }
        // zlib block decompression
        UInt64 remain = e.Uncompressed;
        bool ok = true;
        for (UInt32 bi = 0; bi < e.NumBlocks && ok; bi++)
        {
          const CBlock &b = e.Blocks[bi];
          const UInt64 compSize = b.End - b.Start;
          const UInt64 uncompSize = (e.BlockSize == 0 || remain < e.BlockSize) ? remain : e.BlockSize;
          const HRESULT r = DecodeZlibBlock(_stream, e.Offset + b.Start, compSize, uncompSize, outStream);
          if (r != S_OK)
          {
            extractCallback->SetOperationResult(NArchive::NExtract::NOperationResult::kDataError);
            ok = false;
            break;
          }
          remain -= uncompSize;
          cur += compSize;
        }
        if (ok)
        {
          extractCallback->SetOperationResult(NArchive::NExtract::NOperationResult::kOK);
        }
        continue;
      }
      UInt64 entrySize = StoredDataEntrySize(_info.Version, _info.Is8B);
      UInt64 dataPos = e.Offset + entrySize;
      if (_stream->Seek((Int64)dataPos, STREAM_SEEK_SET, NULL) != S_OK)
      {
        extractCallback->SetOperationResult(NArchive::NExtract::NOperationResult::kDataError);
        cur += e.Size;
        continue;
      }
      UInt64 remain = e.Size;
      Byte buf[1 << 15];
      while (remain)
      {
        UInt32 want = (UInt32)((remain > sizeof(buf)) ? sizeof(buf) : remain);
        UInt32 proc = 0;
        if (_stream->Read(buf, want, &proc) != S_OK || proc == 0)
        {
          extractCallback->SetOperationResult(NArchive::NExtract::NOperationResult::kDataError);
          break;
        }
        if (outStream->Write(buf, proc, &proc) != S_OK)
        {
          extractCallback->SetOperationResult(NArchive::NExtract::NOperationResult::kDataError);
          break;
        }
        remain -= proc;
        cur += proc;
      }
      extractCallback->SetOperationResult(NArchive::NExtract::NOperationResult::kOK);
    }
  }
  return S_OK;
}

Z7_COM7F_IMF(CHandler::GetStream(UInt32 index, ISequentialInStream **stream))
{
  (void)index;
  *stream = NULL;
  return E_NOTIMPL;
}

#ifndef Z7_EXTRACT_ONLY

// ---- UE PAK write support (store-only, create-only) ----
// Serialization follows repak's pak.rs write() / entry.rs write() / footer.rs
// write(): data area -> [simple index | v10+ secondary index] -> FPakInfo.

static const char *kUEMountPoint = "../../../";

struct CWriteEntry
{
  AString Name;        // pak path (ASCII, '/' separators)
  UInt64 Offset;       // absolute offset of the data-area entry; invalid if skipped
  UInt64 Size;         // on-disk size (compressed for zlib, else == Uncompressed)
  UInt64 Uncompressed;
  bool Compressed;     // zlib block-compressed
  UInt32 BlockSize;
  CRecordVector<CBlock> Blocks;
  Byte Hash[20];
  UInt64 EntrySize;    // serialized FPakEntry header size for this file
  Int32 IndexInClient;
};

static int CompareWriteEntries(void *const *p1, void *const *p2, void *)
{
  const CWriteEntry *a = *(const CWriteEntry **)p1;
  const CWriteEntry *b = *(const CWriteEntry **)p2;
  return strcmp(a->Name.Ptr(), b->Name.Ptr());
}

static void WriteU8(Byte *&p, Byte v) { *p++ = v; }
static void WriteU32(Byte *&p, UInt32 v) { SetUi32(p, v); p += 4; }
static void WriteU64(Byte *&p, UInt64 v) { SetUi64(p, v); p += 8; }

static void WriteFString(Byte *&p, const char *s, UInt32 len)
{
  WriteU32(p, len + 1);
  memcpy(p, s, len);
  p += len;
  WriteU8(p, 0);
}

// Compress `size` bytes of `data` in fixed `blockSize` zlib blocks into `out`,
// filling `blocks` with offsets relative to `startOffset`. Returns false on failure.
static bool CompressZlibBlocks(const Byte *data, size_t size, UInt32 blockSize,
    UInt64 startOffset, CRecordVector<CBlock> &blocks, CDynBufSeqOutStream &out)
{
  size_t pos = 0;
  UInt64 off = startOffset;
  while (pos < size)
  {
    const size_t cur = ((size - pos) < blockSize) ? (size - pos) : blockSize;
    CMyComPtr<ISequentialInStream> inStream;
    Create_BufInStream_WithNewBuffer(data + pos, cur, &inStream);
    CMyComPtr<ICompressCoder> enc = new NCompress::NZlib::CEncoder;   // fresh per block (independent state)
    CDynBufSeqOutStream blockOut;
    blockOut.Init();
    UInt64 inSize = cur;
    if (enc->Code(inStream, &blockOut, &inSize, NULL, NULL) != S_OK)
      return false;
    const UInt64 csize = blockOut.GetSize();
    CBlock b;
    b.Start = off;
    b.End = off + csize;
    blocks.Add(b);
    ISequentialOutStream *outS = &out;
    if (outS->Write(blockOut.GetBuffer(), (UInt32)csize, NULL) != S_OK)
      return false;
    off += csize;
    pos += cur;
  }
  return true;
}

// Build one FPakEntry (data-area: offset=0, index: absolute offset).
// `compressed` -> zlib block entry (field = slot 1 + 1 = 2), else store (field 0).
static void WriteEntryInline(Byte *&p, UInt32 version, bool is8b, UInt64 offset,
    UInt64 compSize, UInt64 uncompSize, bool compressed, const CRecordVector<CBlock> &blocks,
    UInt32 blockSize, const Byte *hash)
{
  WriteU64(p, offset);
  WriteU64(p, compSize);
  WriteU64(p, uncompSize);
  const UInt32 field = compressed ? 2 : 0;   // zlib at slot 1 -> field = slot + 1 = 2
  if (IsV8A(version, is8b))
    WriteU8(p, (Byte)field);
  else
    WriteU32(p, field);
  memcpy(p, hash, 20);
  p += 20;
  if (compressed)
  {
    WriteU32(p, (UInt32)blocks.Size());
    for (UInt32 i = 0; i < (UInt32)blocks.Size(); i++)
    {
      WriteU64(p, blocks[i].Start);
      WriteU64(p, blocks[i].End);
    }
  }
  if (version >= 3)
  {
    WriteU8(p, 0);                       // flags (not encrypted)
    WriteU32(p, compressed ? blockSize : 0);
  }
}

static void WriteEntryBuf(CByteBuffer &buf, UInt32 version, bool is8b, UInt64 offset,
    UInt64 compSize, UInt64 uncompSize, bool compressed, const CRecordVector<CBlock> &blocks,
    UInt32 blockSize, const Byte *hash)
{
  UInt64 hdr = 8 + 8 + 8 + (IsV8A(version, is8b) ? 1 : 4) + 20;   // offset,comp,uncomp,compression,hash
  if (compressed)
    hdr += 4 + (UInt64)blocks.Size() * 16;                        // block count + block table
  if (version >= 3)
    hdr += 1 + 4;                                                 // flags + block size
  buf.Alloc((size_t)hdr);
  Byte *p = buf;
  WriteEntryInline(p, version, is8b, offset, compSize, uncompSize, compressed, blocks, blockSize, hash);
}

static void WriteFooter(Byte *&p, UInt32 version, bool is8b, UInt64 indexOffset,
    UInt64 indexSize, const Byte *indexHash, bool zlibNames)
{
  if (version >= 7)
    for (UInt32 i = 0; i < 16; i++) WriteU8(p, 0);   // encryption key guid (none)
  if (version >= 4)
    WriteU8(p, 0);              // bEncryptedIndex = false
  WriteU32(p, kFooterMagic);
  WriteU32(p, version);
  WriteU64(p, indexOffset);
  WriteU64(p, indexSize);
  memcpy(p, indexHash, 20);
  p += 20;
  if (version == 9)
    WriteU8(p, 0);              // bIndexIsFrozen = false
  UInt32 names = (version >= 8) ? (is8b ? 5 : 4) : 0;
  for (UInt32 i = 0; i < names; i++)
  {
    // standard UE4 compression-name table; "Zlib" at slot 1 (field = slot+1 = 2)
    // is what the compressed entries reference. Only populated for zlib output.
    const char *nm = zlibNames ? (i == 1 ? "Zlib" : "") : "";
    const size_t len = zlibNames && i == 1 ? 4 : 0;
    memset(p, 0, 32);
    memcpy(p, nm, len);
    p += 32;
  }
}

Z7_COM7F_IMF(CHandler::SetProperties(const wchar_t * const *names, const PROPVARIANT *values, UInt32 numProps))
{
  for (UInt32 i = 0; i < numProps; i++)
  {
    const UString n = names[i];
    if (n == L"zlib" || n == L"Zlib" || n == L"ZLIB")
    {
      // "zlib" in the parameters field enables zlib-block compression (v8+).
      _useZlib = true;
      continue;
    }
    if (n != L"Version" && n != L"v" && n != L"m")
      continue;
    UInt64 v = 0;
    const PROPVARIANT &pr = values[i];
    if (pr.vt == VT_UI4) v = pr.ulVal;
    else if (pr.vt == VT_UI8) v = pr.uhVal.QuadPart;
    else if (pr.vt == VT_BSTR)
    {
      const UString s(pr.bstrVal);
      for (unsigned k = 0; k < s.Len(); k++)
      {
        const wchar_t c = s[k];
        if (c < L'0' || c > L'9')
          break;
        v = v * 10 + (UInt64)(c - L'0');
      }
    }
    else
      continue;
    // v2..v9 use the simple index layout; v10/v11 (UE5) need the secondary
    // path-hash index generator, which is not implemented yet, so writing is
    // clamped to the UE4 range.
    if (v >= 2 && v <= 9)
      _writeVersion = (UInt32)v;
  }
  return S_OK;
}

Z7_COM7F_IMF(CHandler::GetFileTimeType(UInt32 *type))
{
  *type = NFileTimeType::kWindows;
  return S_OK;
}

Z7_COM7F_IMF(CHandler::UpdateItems(ISequentialOutStream *outStream, UInt32 numItems,
    IArchiveUpdateCallback *callback))
{
  COM_TRY_BEGIN
  if (!callback)
    return E_INVALIDARG;

  const UInt32 version = _writeVersion;
  // major 8 defaults to 8A (4 compression names); 9+ uses 5 names (8B layout)
  const bool is8b = (version >= 9);
  const UInt64 entrySize = StoredDataEntrySize(version, is8b);

  CObjectVector<CWriteEntry> items;

  for (UInt32 i = 0; i < numItems; i++)
  {
    Int32 newData;
    Int32 newProps;
    UInt32 indexInArc;
    RINOK(callback->GetUpdateItemInfo(i, &newData, &newProps, &indexInArc))
    if (!IntToBool(newData))
      continue;
    {
      NCOM::CPropVariant prop;
      RINOK(callback->GetProperty(i, kpidIsDir, &prop))
      if (prop.vt == VT_BOOL && prop.boolVal != VARIANT_FALSE)
        continue;
    }
    CWriteEntry e;
    e.IndexInClient = (Int32)i;
    e.Offset = 0;
    e.Size = 0;
    e.Uncompressed = 0;
    e.Compressed = false;
    e.BlockSize = 0;
    e.EntrySize = 0;
    e.Blocks.Clear();
    memset(e.Hash, 0, sizeof(e.Hash));
    {
      NCOM::CPropVariant prop;
      RINOK(callback->GetProperty(i, kpidPath, &prop))
      if (prop.vt != VT_BSTR)
        return E_INVALIDARG;
      const UString s = prop.bstrVal;
      AString a;
      for (unsigned k = 0; k < s.Len(); k++)
      {
        wchar_t c = s[k];
        if (c == L'\\')
          c = L'/';
        if (c >= 0x80)
          return E_INVALIDARG;   // UE pak paths are ASCII
        a += (char)c;
      }
      e.Name = a;
    }
    if (e.Name.IsEmpty())
      return E_INVALIDARG;
    {
      NCOM::CPropVariant prop;
      RINOK(callback->GetProperty(i, kpidSize, &prop))
      if (prop.vt != VT_UI8)
        return E_INVALIDARG;
      e.Size = prop.uhVal.QuadPart;
      e.Uncompressed = e.Size;
    }
    items.Add(e);
  }

  // UE index is a sorted map keyed by path
  if (items.Size() >= 2)
    items.Sort(CompareWriteEntries, NULL);

  const char *mount = kUEMountPoint;
  const UInt32 mountLen = (UInt32)strlen(mount);

  UInt64 indexSize = 4 + mountLen + 1 + 4;   // mount FString + record count
  for (UInt32 k = 0; k < (UInt32)items.Size(); k++)
    indexSize += 4 + (UInt64)items[k].Name.Len() + 1 + entrySize;

  const UInt64 footerSize = PakInfoSize(version, is8b);

  UInt64 total = 0;
  for (UInt32 k = 0; k < (UInt32)items.Size(); k++)
    total += entrySize + items[k].Size;
  total += indexSize + footerSize;
  RINOK(callback->SetTotal(total))

  // ---- write data area (buffer each file to compute its SHA-1 / zlib first) ----
  UInt64 filePos = 0;
  UInt64 done = 0;
  for (UInt32 k = 0; k < (UInt32)items.Size(); k++)
  {
    CWriteEntry &e = items[k];
    CMyComPtr<ISequentialInStream> inStream;
    const HRESULT res = callback->GetStream((UInt32)e.IndexInClient, &inStream);
    if (res == S_FALSE || !inStream)
    {
      e.Offset = (UInt64)(Int64)-1;   // skipped - omit from data and index
      continue;
    }
    RINOK(res)

    if (e.Uncompressed > ((UInt64)1 << 30))
      return E_OUTOFMEMORY;   // do not buffer files larger than 1 GiB
    CByteBuffer dataBuf((size_t)e.Uncompressed);
    if (!dataBuf)
      return E_OUTOFMEMORY;
    {
      Byte *bp = (Byte *)dataBuf;
      UInt64 remaining = e.Uncompressed;
      while (remaining)
      {
        size_t want = (size_t)((remaining > ((UInt64)1 << 20)) ? ((UInt64)1 << 20) : remaining);
        size_t processed = want;
        RINOK(ReadStream(inStream, bp, &processed))
        if (processed == 0)
          break;              // unexpected EOF
        bp += processed;
        remaining -= processed;
      }
      if (remaining)
      {
        e.Offset = (UInt64)(Int64)-1;   // short read - skip
        continue;
      }
    }

    // optional zlib block compression (v8+ only; small files stay store)
    CDynBufSeqOutStream compBuf;   // holds the concatenated zlib blocks when compressed
    compBuf.Init();
    e.Compressed = false;
    if (_useZlib && version >= 8 && e.Uncompressed >= 100)
    {
      const UInt32 bs = (e.Uncompressed < 65536) ? (UInt32)e.Uncompressed : 65536;
      const UInt64 blockCount = (e.Uncompressed == 0) ? 0 : (1 + ((e.Uncompressed - 1) / bs));
      const UInt64 startOffset = StoredDataEntrySize(version, is8b) + 4 + blockCount * 16;
      e.Blocks.Clear();
      if (CompressZlibBlocks((const Byte *)dataBuf, (size_t)e.Uncompressed, bs, startOffset, e.Blocks, compBuf))
      {
        if ((UInt64)compBuf.GetSize() + blockCount * 16 < e.Uncompressed)
        {
          e.Compressed = true;
          e.BlockSize = bs;
          e.Size = compBuf.GetSize();
        }
      }
    }

    // SHA-1 over the on-disk bytes (compressed stream if zlib, else the original)
    {
      const Byte *hashSrc = (const Byte *)dataBuf;
      size_t hashLen = (size_t)e.Uncompressed;
      if (e.Compressed) { hashSrc = compBuf.GetBuffer(); hashLen = compBuf.GetSize(); }
      CSha1 sha;
      Sha1_Init(&sha);
      Sha1_Update(&sha, hashSrc, hashLen);
      Sha1_Final(&sha, e.Hash);
    }

    e.EntrySize = e.Compressed
        ? (StoredDataEntrySize(version, is8b) + 4 + (UInt64)e.Blocks.Size() * 16)
        : StoredDataEntrySize(version, is8b);
    e.Offset = filePos;
    {
      CByteBuffer entryBuf;
      WriteEntryBuf(entryBuf, version, is8b, 0, e.Size, e.Uncompressed, e.Compressed, e.Blocks, e.BlockSize, e.Hash);
      RINOK(WriteStream(outStream, (const Byte *)entryBuf, entryBuf.Size()))
    }
    if (e.Compressed)
      RINOK(WriteStream(outStream, compBuf.GetBuffer(), compBuf.GetSize()))
    else
      RINOK(WriteStream(outStream, (const Byte *)dataBuf, (size_t)e.Uncompressed))

    filePos += e.EntrySize + e.Size;
    done += e.EntrySize + e.Size;
    RINOK(callback->SetCompleted(&done))
    RINOK(callback->SetOperationResult(NArchive::NUpdate::NOperationResult::kOK))
  }

  // ---- build + write index (simple layout) ----
  {
    const UInt64 indexStart = filePos;   // absolute offset where the index begins
    UInt64 actualIndexSize = 4 + mountLen + 1 + 4;   // mount FString + record count
    UInt32 writtenCount = 0;
    for (UInt32 k = 0; k < (UInt32)items.Size(); k++)
      if (items[k].Offset != (UInt64)(Int64)-1)
      {
        writtenCount++;
        actualIndexSize += 4 + (UInt64)items[k].Name.Len() + 1 + items[k].EntrySize;
      }
    CByteBuffer indexBuf((size_t)actualIndexSize);
    if (!indexBuf)
      return E_OUTOFMEMORY;
    {
      Byte *p = indexBuf;
      WriteFString(p, mount, mountLen);
      WriteU32(p, writtenCount);
      for (UInt32 k = 0; k < (UInt32)items.Size(); k++)
      {
        const CWriteEntry &e = items[k];
        if (e.Offset == (UInt64)(Int64)-1)
          continue;
        WriteFString(p, e.Name.Ptr(), (UInt32)e.Name.Len());
        WriteEntryInline(p, version, is8b, e.Offset, e.Size, e.Uncompressed, e.Compressed, e.Blocks, e.BlockSize, e.Hash);
      }
    }
    Byte indexHash[20];
    {
      CSha1 sha;
      Sha1_Init(&sha);
      Sha1_Update(&sha, (const Byte *)indexBuf, (size_t)actualIndexSize);
      Sha1_Final(&sha, indexHash);
    }
    RINOK(WriteStream(outStream, (const Byte *)indexBuf, (size_t)actualIndexSize))
    done += actualIndexSize;
    RINOK(callback->SetCompleted(&done))

    Byte footer[512];
    Byte *fp = footer;
    WriteFooter(fp, version, is8b, indexStart, actualIndexSize, indexHash, (_useZlib && version >= 8));
    RINOK(WriteStream(outStream, footer, (size_t)(fp - footer)))
    done += footerSize;
    RINOK(callback->SetCompleted(&done))
  }

  return S_OK;
  COM_TRY_END
}

#endif // !Z7_EXTRACT_ONLY


static const Byte kArcFlags = 0;   // keep writable in multi-file add dialog
                                    // (kKeepName would hide it when !oneFile)

static UInt32 WINAPI IsArc_UEPak(const Byte *p, size_t size)
{
  // UE PAK signature lives at the END of the file; a start-of-file probe
  // cannot confirm it. Return NEED_MORE so routing is decided by Open()
  // (extension + footer) rather than by a false start-scan.
  (void)p; (void)size;
  return k_IsArc_Res_NEED_MORE;
}

#ifndef Z7_EXTRACT_ONLY
#define REGISTER_ARC_IO_CLS_NO_SIG(cls, n, e, ae, id, offs, flags, tf, isArc) \
  IMP_CreateArcIn_2(cls) \
  IMP_CreateArcOut \
  REGISTER_ARC_R(n, e, ae, id, 0, NULL, offs, flags, tf, CreateArc, CreateArcOut, isArc)
REGISTER_ARC_IO_CLS_NO_SIG(CHandler(),
    "pak(UE4)", "pak", "", 0xE9, 0, kArcFlags, 0, IsArc_UEPak)
#else
REGISTER_ARC_I_CLS_NO_SIG(CHandler(),
    "pak(UE4)", "pak", "", 0xE9, 0, kArcFlags, IsArc_UEPak)
#endif

}}
