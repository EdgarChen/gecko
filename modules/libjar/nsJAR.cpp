/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is Mozilla Communicator client code, 
 * released March 31, 1998. 
 *
 * The Initial Developer of the Original Code is Netscape Communications 
 * Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s): 
 *     Daniel Veditz <dveditz@netscape.com>
 *     Samir Gehani <sgehani@netscape.com>
 *     Mitch Stoltz <mstoltz@netsape.com>
 *     Pierre Phaneuf <pp@ludusdesign.com>
 */
#include <string.h>
#include "nsILocalFile.h"
#include "nsJARInputStream.h"
#include "nsJAR.h"
#include "nsXPIDLString.h"
#include "nsIServiceManager.h"
#include "plbase64.h"
#include "nsIConsoleService.h"

#ifdef XP_UNIX
  #include <sys/stat.h>
#elif defined (XP_PC)
  #include <io.h>
#endif

//----------------------------------------------
// Errors and other utility definitions
//----------------------------------------------
#ifndef __gen_nsIFile_h__
#define NS_ERROR_FILE_UNRECOGNIZED_PATH         NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_FILES, 1)
#define NS_ERROR_FILE_UNRESOLVABLE_SYMLINK      NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_FILES, 2)
#define NS_ERROR_FILE_EXECUTION_FAILED          NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_FILES, 3)
#define NS_ERROR_FILE_UNKNOWN_TYPE              NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_FILES, 4)
#define NS_ERROR_FILE_DESTINATION_NOT_DIR       NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_FILES, 5)
#define NS_ERROR_FILE_TARGET_DOES_NOT_EXIST     NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_FILES, 6)
#define NS_ERROR_FILE_COPY_OR_MOVE_FAILED       NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_FILES, 7)
#define NS_ERROR_FILE_ALREADY_EXISTS            NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_FILES, 8)
#define NS_ERROR_FILE_INVALID_PATH              NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_FILES, 9)
#define NS_ERROR_FILE_DISK_FULL                 NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_FILES, 10)
#define NS_ERROR_FILE_CORRUPTED                 NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_FILES, 11)
#endif

static nsresult
ziperr2nsresult(PRInt32 ziperr)
{
  switch (ziperr) {
    case ZIP_OK:                return NS_OK;
    case ZIP_ERR_MEMORY:        return NS_ERROR_OUT_OF_MEMORY;
    case ZIP_ERR_DISK:          return NS_ERROR_FILE_DISK_FULL;
    case ZIP_ERR_CORRUPT:       return NS_ERROR_FILE_CORRUPTED;
    case ZIP_ERR_PARAM:         return NS_ERROR_ILLEGAL_VALUE;
    case ZIP_ERR_FNF:           return NS_ERROR_FILE_TARGET_DOES_NOT_EXIST;
    case ZIP_ERR_UNSUPPORTED:   return NS_ERROR_NOT_IMPLEMENTED;
    default:                    return NS_ERROR_FAILURE;
  }
}

//-- PR_Free doesn't null the pointer. 
//   This macro takes care of that.
#define JAR_NULLFREE(_ptr) \
  {                        \
    PR_FREEIF(_ptr);       \
    _ptr = nsnull;         \
  }

//----------------------------------------------
// nsJARManifestItem declaration
//----------------------------------------------
/*
 * nsJARManifestItem contains meta-information pertaining 
 * to an individual JAR entry, taken from the 
 * META-INF/MANIFEST.MF and META-INF/ *.SF files.
 * This is security-critical information, defined here so it is not
 * accessible from anywhere else.
 */
typedef enum
{
  JAR_INVALID       = 1,
  JAR_INTERNAL      = 2,
  JAR_EXTERNAL      = 3
} JARManifestItemType;

class nsJARManifestItem
{
public:
  JARManifestItemType mType;

  // True if the second step of verification (VerifyEntry) 
  // has taken place:
  PRBool              entryVerified;
  
  // Not signed, valid, or failure code
  PRInt16             status;
  
  // Internal storage of digests
  char*               calculatedSectionDigest;
  char*               storedEntryDigest;

  nsJARManifestItem();
  virtual ~nsJARManifestItem();
};

//-------------------------------------------------
// nsJARManifestItem constructors and destructor
//-------------------------------------------------
nsJARManifestItem::nsJARManifestItem(): mType(JAR_INTERNAL),
                                        entryVerified(PR_FALSE),
                                        status(nsIZipReader::NOT_SIGNED),
                                        calculatedSectionDigest(nsnull),
                                        storedEntryDigest(nsnull)
{
}

nsJARManifestItem::~nsJARManifestItem()
{
  // Delete digests if necessary
  PR_FREEIF(calculatedSectionDigest);
  PR_FREEIF(storedEntryDigest);
}

//----------------------------------------------
// nsJAR constructor/destructor
//----------------------------------------------
PR_STATIC_CALLBACK(PRBool)
DeleteManifestEntry(nsHashKey* aKey, void* aData, void* closure)
{
//-- deletes an entry in  mManifestData.
  PR_FREEIF(aData);
  return PR_TRUE;
}

// The following initialization makes a guess of 10 entries per jarfile.
nsJAR::nsJAR(): mManifestData(nsnull, nsnull, DeleteManifestEntry, nsnull, 10),
                mParsedManifest(PR_FALSE), mGlobalStatus(nsIZipReader::NOT_SIGNED),
                mReleaseTime(0), mCache(nsnull)
{
  NS_INIT_REFCNT();
}

nsJAR::~nsJAR()
{
  Close();
}

NS_IMPL_THREADSAFE_QUERY_INTERFACE1(nsJAR, nsIZipReader)
NS_IMPL_THREADSAFE_ADDREF(nsJAR)

// Custom Release method works with nsZipReaderCache...
nsrefcnt nsJAR::Release(void) 
{
  nsrefcnt count; 
  NS_PRECONDITION(0 != mRefCnt, "dup release"); 
  count = PR_AtomicDecrement((PRInt32 *)&mRefCnt); 
  NS_LOG_RELEASE(this, count, "nsJAR"); 
  if (0 == count) {
    mRefCnt = 1; /* stabilize */ 
    /* enable this to find non-threadsafe destructors: */ 
    /* NS_ASSERT_OWNINGTHREAD(_class); */ 
    NS_DELETEXPCOM(this); 
    return 0; 
  }
  else if (1 == count && mCache) {
    mReleaseTime = PR_IntervalNow();
    nsresult rv = mCache->ReleaseZip(this);
    NS_ASSERTION(NS_SUCCEEDED(rv), "failed to release zip file");
  }
  return count; 
} 

//----------------------------------------------
// nsJAR public implementation
//----------------------------------------------

NS_METHOD
nsJAR::Create(nsISupports *aOuter, REFNSIID aIID, void **aResult)
{
    if (aOuter)
        return NS_ERROR_NO_AGGREGATION;

    nsJAR* jar = new nsJAR();
    if (jar == nsnull)
        return NS_ERROR_OUT_OF_MEMORY;
    NS_ADDREF(jar);
    nsresult rv = jar->QueryInterface(aIID, aResult);
    NS_RELEASE(jar);
    return rv;
}

NS_IMETHODIMP
nsJAR::Init(nsIFile* zipFile)
{
  mZipFile = zipFile;
  return NS_OK;
}

NS_IMETHODIMP
nsJAR::GetFile(nsIFile* *result)
{
  *result = mZipFile;
  NS_ADDREF(*result);
  return NS_OK;
}

NS_IMETHODIMP
nsJAR::Open()
{
  nsresult rv;
  nsCOMPtr<nsILocalFile> localFile = do_QueryInterface(mZipFile, &rv);
  if (NS_FAILED(rv)) return rv;

  PRFileDesc* fd;
  rv = localFile->OpenNSPRFileDesc(PR_RDONLY, 0664, &fd);
  if (NS_FAILED(rv)) return rv;

  PRInt32 err = mZip.OpenArchiveWithFileDesc(fd);
  
  return ziperr2nsresult(err);
}

NS_IMETHODIMP
nsJAR::Close()
{
  PRInt32 err = mZip.CloseArchive();
  return ziperr2nsresult(err);
}

NS_IMETHODIMP
nsJAR::Extract(const char *zipEntry, nsIFile* outFile)
{
  nsresult rv;
  nsCOMPtr<nsILocalFile> localFile = do_QueryInterface(outFile, &rv);
  if (NS_FAILED(rv)) return rv;

  PRFileDesc* fd;
  rv = localFile->OpenNSPRFileDesc(PR_RDWR | PR_CREATE_FILE, 0664, &fd);
  if (NS_FAILED(rv)) return rv;

  nsZipItem *item = 0;
  PRInt32 err = mZip.ExtractFileToFileDesc(zipEntry, fd, &item);
  PR_Close(fd);

  if (err != ZIP_OK)
    outFile->Delete(PR_FALSE);
  else
  {
#if defined(XP_UNIX)
    char *path;

    rv = outFile->GetPath(&path); 
    if (NS_SUCCEEDED(rv))
      chmod(path, item->mode);
#endif

    RestoreModTime(item, outFile);  // non-fatal if this fails, ignore errors
  }

  return ziperr2nsresult(err);
}

NS_IMETHODIMP    
nsJAR::GetEntry(const char *zipEntry, nsIZipEntry* *result)
{
  nsZipItem* zipItem;
  PRInt32 err = mZip.GetItem(zipEntry, &zipItem);
  if (err != ZIP_OK) return ziperr2nsresult(err);

  nsJARItem* jarItem = new nsJARItem();
  if (jarItem == nsnull)
    return NS_ERROR_OUT_OF_MEMORY;
  NS_ADDREF(jarItem);
  jarItem->Init(zipItem);
  *result = jarItem;
  return NS_OK;
}

NS_IMETHODIMP    
nsJAR::FindEntries(const char *aPattern, nsISimpleEnumerator **result)
{
  if (!result)
    return NS_ERROR_INVALID_POINTER;
    
  nsZipFind *find = mZip.FindInit(aPattern);
  if (!find)
    return NS_ERROR_OUT_OF_MEMORY;

  nsISimpleEnumerator *zipEnum = new nsJAREnumerator(find);
  if (!zipEnum)
    return NS_ERROR_OUT_OF_MEMORY;
  NS_ADDREF( zipEnum );

  *result = zipEnum;
  return NS_OK;
}

NS_IMETHODIMP
nsJAR::GetInputStream(const char* aFilename, nsIInputStream** result)
{
  NS_ENSURE_ARG_POINTER(result);
  nsresult rv;
  nsJARInputStream* jis = nsnull;
  rv = nsJARInputStream::Create(nsnull, NS_GET_IID(nsIInputStream), (void**)&jis);
  if (!jis) return NS_ERROR_FAILURE;

  rv = jis->Init(this, aFilename);
  if (NS_FAILED(rv)) return NS_ERROR_FAILURE;

  *result = (nsIInputStream*)jis;
  return NS_OK;
}

NS_IMETHODIMP
nsJAR::GetCertificatePrincipal(const char* aFilename, nsIPrincipal** aPrincipal)
{
  //-- Parameter check
  if (!aPrincipal)
    return NS_ERROR_NULL_POINTER;
  *aPrincipal = nsnull;

  //-- Get the signature verifier service
  nsresult rv;
  NS_WITH_SERVICE(nsISignatureVerifier, verifier, SIGNATURE_VERIFIER_PROGID, &rv);
  if (NS_FAILED(rv)) // No signature verifier available
    return NS_OK;

  //-- Parse the manifest
  rv = ParseManifest(verifier);
  if (NS_FAILED(rv)) return rv;
  if (mGlobalStatus == nsIZipReader::NO_MANIFEST)
    return NS_OK;

  PRInt16 requestedStatus;
  if (aFilename)
  {
    //-- Find the item
    nsCStringKey key(aFilename);
    nsJARManifestItem* manItem = (nsJARManifestItem*)mManifestData.Get(&key);
    if (!manItem)
      return NS_OK;
    //-- Verify the item against the manifest
    if (!manItem->entryVerified)
    {
      nsXPIDLCString entryData;
      PRUint32 entryDataLen;
      rv = LoadEntry(aFilename, getter_Copies(entryData), &entryDataLen);
      if (NS_FAILED(rv)) return rv;
      rv = VerifyEntry(verifier, manItem, entryData, entryDataLen);
      if (NS_FAILED(rv)) return rv;
    }
    requestedStatus = manItem->status;
  }
  else // User wants identity of signer w/o verifying any entries
    requestedStatus = mGlobalStatus;

  if (requestedStatus != nsIZipReader::VALID)
    ReportError(aFilename, requestedStatus);
  else // Valid signature
  {
    *aPrincipal = mPrincipal;
    NS_IF_ADDREF(*aPrincipal);
  }
  return NS_OK;
}

//----------------------------------------------
// nsJAR private implementation
//----------------------------------------------
nsresult 
nsJAR::LoadEntry(const char* aFilename, char** aBuf, PRUint32* aBufLen)
{
  //-- Get a stream for reading the file
  nsresult rv;
  nsCOMPtr<nsIInputStream> manifestStream;
  rv = GetInputStream(aFilename, getter_AddRefs(manifestStream));
  if (NS_FAILED(rv)) return NS_ERROR_FILE_TARGET_DOES_NOT_EXIST;
  
  //-- Read the manifest file into memory
  char* buf;
  PRUint32 len;
  rv = manifestStream->Available(&len);
  if (NS_FAILED(rv)) return rv;
  buf = (char*)PR_MALLOC(len+1);
  if (!buf) return NS_ERROR_OUT_OF_MEMORY;
  PRUint32 bytesRead;
  rv = manifestStream->Read(buf, len, &bytesRead);
  if (bytesRead != len) 
    rv = NS_ERROR_FILE_CORRUPTED;
  if (NS_FAILED(rv)) return rv;
  buf[len] = '\0'; //Null-terminate the buffer
  *aBuf = buf;
  if (aBufLen)
    *aBufLen = len;
  return NS_OK;
}


PRInt32
nsJAR::ReadLine(const char** src)
{
  //--Moves pointer to beginning of next line and returns line length
  //  not including CR/LF.
  PRInt32 length;
  char* eol = PL_strpbrk(*src, "\r\n");

  if (eol == nsnull) // Probably reached end of file before newline
  {
    length = PL_strlen(*src);
    if (length == 0) // immediate end-of-file
      *src = nsnull;
    else             // some data left on this line
      *src += length;
  }
  else
  {
    length = eol - *src;
    if (eol[0] == '\r' && eol[1] == '\n')      // CR LF, so skip 2
      *src = eol+2;
    else                                       // Either CR or LF, so skip 1
      *src = eol+1;
  }
  return length;
}

//-- The following #defines are used by ParseManifest()
//   and ParseOneFile(). The header strings are defined in the JAR specification.
#define JAR_MF 1
#define JAR_SF 2
#define JAR_MF_SEARCH_STRING "(M|/M)ETA-INF/(M|m)(ANIFEST|anifest).(MF|mf)$"
#define JAR_SF_SEARCH_STRING "(M|/M)ETA-INF/*.(SF|sf)$"
#define JAR_MF_HEADER (const char*)"Manifest-Version: 1.0"
#define JAR_SF_HEADER (const char*)"Signature-Version: 1.0"

nsresult
nsJAR::ParseManifest(nsISignatureVerifier* verifier)
{
  //-- Verification Step 1
  if (mParsedManifest)
    return NS_OK;
  //-- (1)Manifest (MF) file
  nsresult rv;
  nsCOMPtr<nsISimpleEnumerator> files;
  rv = FindEntries(JAR_MF_SEARCH_STRING, getter_AddRefs(files));
  if (!files) rv = NS_ERROR_FAILURE;
  if (NS_FAILED(rv)) return rv;

  //-- Load the file into memory
  nsCOMPtr<nsJARItem> file;
  rv = files->GetNext(getter_AddRefs(file));
  if (NS_FAILED(rv)) return rv;
  if (!file)
  {
    mGlobalStatus = nsIZipReader::NO_MANIFEST;
    mParsedManifest = PR_TRUE;
    return NS_OK;
  }
  PRBool more;
  rv = files->HasMoreElements(&more);
  if (NS_FAILED(rv)) return rv;
  if (more)
  {
    mParsedManifest = PR_TRUE;
    return NS_ERROR_FILE_CORRUPTED; // More than one MF file
  }
  nsXPIDLCString manifestFilename;
  rv = file->GetName(getter_Copies(manifestFilename));
  if (!manifestFilename || NS_FAILED(rv)) return rv;
  nsXPIDLCString manifestBuffer;
  rv = LoadEntry(manifestFilename, getter_Copies(manifestBuffer));
  if (NS_FAILED(rv)) return rv;

  //-- Parse it
  rv = ParseOneFile(verifier, manifestBuffer, JAR_MF);
  if (NS_FAILED(rv)) return rv;

  //-- (2)Signature (SF) file
  // If there are multiple signatures, we select one.
  rv = FindEntries(JAR_SF_SEARCH_STRING, getter_AddRefs(files));
  if (!files) rv = NS_ERROR_FAILURE;
  if (NS_FAILED(rv)) return rv;
  //-- Get an SF file
  rv = files->GetNext(getter_AddRefs(file));
  if (NS_FAILED(rv)) return rv;
  if (!file)
  {
    mGlobalStatus = nsIZipReader::NO_MANIFEST;
    mParsedManifest = PR_TRUE;
    return NS_OK;
  }
  rv = file->GetName(getter_Copies(manifestFilename));
  if (NS_FAILED(rv)) return rv;

  PRUint32 manifestLen;
  rv = LoadEntry(manifestFilename, getter_Copies(manifestBuffer), &manifestLen);
  if (NS_FAILED(rv)) return rv;
  
  //-- Get its corresponding signature file
  nsCAutoString sigFilename( NS_STATIC_CAST(const char*, manifestFilename) );
  PRInt32 extension = sigFilename.RFindChar('.') + 1;
  NS_ASSERTION(extension != 0, "Manifest Parser: Missing file extension.");
  (void)sigFilename.Cut(extension, 2);
  nsXPIDLCString sigBuffer;
  PRUint32 sigLen;
  {
    nsCAutoString tempFilename(sigFilename); tempFilename.Append("rsa", 3);
    rv = LoadEntry(tempFilename, getter_Copies(sigBuffer), &sigLen);
  }
  if (NS_FAILED(rv))
    {
      nsCAutoString tempFilename(sigFilename); tempFilename.Append("RSA", 3);
      rv = LoadEntry(tempFilename, getter_Copies(sigBuffer), &sigLen);
    }
  if (NS_FAILED(rv))
  {
    mGlobalStatus = nsIZipReader::NO_MANIFEST;
    mParsedManifest = PR_TRUE;
    return NS_OK;
  }
  
  //-- Verify that the signature file is a valid signature of the SF file
  PRInt32 verifyError;
  rv = verifier->VerifySignature(sigBuffer, sigLen, manifestBuffer, manifestLen, 
                                 &verifyError, getter_AddRefs(mPrincipal));
  if (NS_FAILED(rv)) return rv;
  if (mPrincipal)
    mGlobalStatus = nsIZipReader::VALID;
  else if (verifyError == nsISignatureVerifier::VERIFY_ERROR_UNKNOWN_CA)
    mGlobalStatus = nsIZipReader::INVALID_UNKNOWN_CA;
  else
    mGlobalStatus = nsIZipReader::INVALID_SIG;

  //-- Parse the SF file. If the verification above failed, principal
  // is null, and ParseOneFile will mark the relevant entries as invalid.
  // if ParseOneFile fails, then it has no effect, and we can safely 
  // continue to the next SF file, or return. 
  ParseOneFile(verifier, manifestBuffer, JAR_SF);
  mParsedManifest = PR_TRUE;

  return NS_OK;
}

nsresult
nsJAR::ParseOneFile(nsISignatureVerifier* verifier,
                    const char* filebuf, PRInt16 aFileType)
{
  //-- Check file header
  const char* nextLineStart = filebuf;
  nsCAutoString curLine;
  PRInt32 linelen;
  linelen = ReadLine(&nextLineStart);
  curLine.Assign(filebuf, linelen);

  if ( ((aFileType == JAR_MF) && !curLine.Equals(JAR_MF_HEADER) ) ||
       ((aFileType == JAR_SF) && !curLine.Equals(JAR_SF_HEADER) ) )
     return NS_ERROR_FILE_CORRUPTED;

  //-- Skip header section
  do {
    linelen = ReadLine(&nextLineStart);
  } while (linelen > 0);

  //-- Set up parsing variables
  const char* curPos;
  const char* sectionStart = nextLineStart;

  nsJARManifestItem* curItemMF;
  PRBool foundName = PR_FALSE;
  if (aFileType == JAR_MF)
    curItemMF = new nsJARManifestItem();
  nsCAutoString curItemName;
  nsCAutoString storedSectionDigest;

  for(;;)
  {
    curPos = nextLineStart;
    linelen = ReadLine(&nextLineStart);
    curLine.Assign(curPos, linelen);
    if (linelen == 0) 
    // end of section (blank line or end-of-file)
    {
      if (aFileType == JAR_MF)
      {
        if (curItemMF->mType != JAR_INVALID)
        { 
          //-- Did this section have a name: line?
          if(!foundName)
            curItemMF->mType = JAR_INVALID;
          else 
          {
            if (curItemMF->mType == JAR_INTERNAL)
            {
            //-- If it's an internal item, it must correspond 
            //   to a valid jar entry
              nsIZipEntry* entry;
              PRInt32 result = GetEntry(curItemName, &entry);
              if (result != ZIP_OK || !entry)
                curItemMF->mType = JAR_INVALID;
            }
            //-- Check for duplicates
            nsCStringKey key(curItemName);
            if (mManifestData.Exists(&key))
              curItemMF->mType = JAR_INVALID;
          }
        }

        if (curItemMF->mType == JAR_INVALID)
          delete curItemMF;
        else //-- calculate section digest
        {
          PRUint32 sectionLength = curPos - sectionStart;
          CalculateDigest(verifier, sectionStart, sectionLength,
                          &(curItemMF->calculatedSectionDigest));
          //-- Save item in the hashtable
          nsCStringKey itemKey(curItemName);
          mManifestData.Put(&itemKey, (void*)curItemMF);
        }
        if (nextLineStart == nsnull) // end-of-file
          break;

        sectionStart = nextLineStart;
        curItemMF = new nsJARManifestItem();
      } // (aFileType == JAR_MF)
      else
        //-- file type is SF, compare digest with calculated 
        //   section digests from MF file.
      {
        if (foundName)
        {
          nsJARManifestItem* curItemSF;
          nsCStringKey key(curItemName);
          curItemSF = (nsJARManifestItem*)mManifestData.Get(&key);
          if(curItemSF)
          {
            NS_ASSERTION(curItemSF->status == nsJAR::NOT_SIGNED,
                         "SECURITY ERROR: nsJARManifestItem not correctly initialized");
            curItemSF->status = mGlobalStatus;
            if (curItemSF->status == nsIZipReader::VALID)
            { // Compare digests
              if (storedSectionDigest.Length() == 0)
                curItemSF->status = nsIZipReader::NOT_SIGNED;
              else
              {
                if (!storedSectionDigest.Equals((const char*)curItemSF->calculatedSectionDigest))
                  curItemSF->status = nsIZipReader::INVALID_MANIFEST;
                JAR_NULLFREE(curItemSF->calculatedSectionDigest)
                storedSectionDigest = "";
              }
            } // (aPrincipal != nsnull)
          } // if(curItemSF)
        } // if(foundName)

        if(nextLineStart == nsnull) // end-of-file
          break;
      } // aFileType == JAR_SF
      foundName = PR_FALSE;
      continue;
    } // if(linelen == 0)

    //-- Look for continuations (beginning with a space) on subsequent lines
    //   and append them to the current line.
    while(*nextLineStart == ' ')
    {
      curPos = nextLineStart;
      PRInt32 continuationLen = ReadLine(&nextLineStart) - 1;
      nsCAutoString continuation(curPos+1, continuationLen);
      curLine += continuation;
      linelen += continuationLen;
    }

    //-- Find colon in current line, this separates name from value
    PRInt32 colonPos = curLine.FindChar(':');
    if (colonPos == -1)    // No colon on line, ignore line
      continue;
    //-- Break down the line
    nsCAutoString lineName;
    curLine.Left(lineName, colonPos);
    nsCAutoString lineData;
    curLine.Mid(lineData, colonPos+2, linelen - (colonPos+2));

    //-- Lines to look for:
    // (1) Digest:
    if (lineName.CompareWithConversion("SHA1-Digest",PR_TRUE) == 0) 
    //-- This is a digest line, save the data in the appropriate place 
    {
      if(aFileType == JAR_MF)
      {
        curItemMF->storedEntryDigest = (char*)PR_MALLOC(lineData.Length()+1);
        if (!(curItemMF->storedEntryDigest))
          return NS_ERROR_OUT_OF_MEMORY;
        PL_strcpy(curItemMF->storedEntryDigest, lineData);
      }
      else
        storedSectionDigest = lineData;
      continue;
    }
    
    // (2) Name: associates this manifest section with a file in the jar.
    if (!foundName && lineName.CompareWithConversion("Name", PR_TRUE) == 0) 
    {
      curItemName = lineData;
      foundName = PR_TRUE;
      continue;
    }

    // (3) Magic: this may be an inline Javascript. 
    //     We can't do any other kind of magic.
    if ( aFileType == JAR_MF &&
         lineName.CompareWithConversion("Magic", PR_TRUE) == 0) 
    {
      if(lineData.CompareWithConversion("javascript", PR_TRUE) == 0)
        curItemMF->mType = JAR_EXTERNAL;
      else
        curItemMF->mType = JAR_INVALID;
      continue;
    }

  } // for (;;)
  return NS_OK;
} //ParseOneFile()

nsresult
nsJAR::VerifyEntry(nsISignatureVerifier* verifier,
                   nsJARManifestItem* aManItem, const char* aEntryData,
                   PRUint32 aLen)
{
  if (aManItem->status == nsIZipReader::VALID)
  {
    if(!aManItem->storedEntryDigest)
      // No entry digests in manifest file. Entry is unsigned.
      aManItem->status = nsIZipReader::NOT_SIGNED;
    else
    { //-- Calculate and compare digests
      char* calculatedEntryDigest;
      nsresult rv = CalculateDigest(verifier, aEntryData, aLen, &calculatedEntryDigest);
      if (NS_FAILED(rv)) return NS_ERROR_FAILURE;
      if (PL_strcmp(aManItem->storedEntryDigest, calculatedEntryDigest) != 0)
        aManItem->status = nsIZipReader::INVALID_ENTRY;
      JAR_NULLFREE(calculatedEntryDigest)
      JAR_NULLFREE(aManItem->storedEntryDigest)
    }
  }
  aManItem->entryVerified = PR_TRUE;
  return NS_OK;
}

void nsJAR::ReportError(const char* aFilename, PRInt16 errorCode)
{
  //-- Generate error message
  nsAutoString message; 
  message.AssignWithConversion("Signature Verification Error: the signature on ");
  if (aFilename)
    message.AppendWithConversion(aFilename);
  else
    message.AppendWithConversion("this .jar archive");
  message.AppendWithConversion(" is invalid because ");
  switch(errorCode)
  {
  case nsIZipReader::NOT_SIGNED:
    message.AppendWithConversion("the archive did not contain a valid PKCS7 signature.");
    break;
  case nsIZipReader::INVALID_SIG:
    message.AppendWithConversion("the digital signature (*.RSA) file is not a valid signature of the signature instruction file (*.SF).");
    break;
  case nsIZipReader::INVALID_UNKNOWN_CA:
    message.AppendWithConversion("the certificate used to sign this file has an unrecognized issuer.");
    break;
  case nsIZipReader::INVALID_MANIFEST:
    message.AppendWithConversion("the signature instruction file (*.SF) does not contain a valid hash of the MANIFEST.MF file.");
    break;
  case nsIZipReader::INVALID_ENTRY:
    message.AppendWithConversion("the MANIFEST.MF file does not contain a valid hash of the file being verified.");
    break;
  default:
    message.AppendWithConversion("of an unknown problem.");
  }
  
  // Report error in JS console
  nsCOMPtr<nsIConsoleService> console(do_GetService("mozilla.consoleservice.1"));
  if (console)
  {
    PRUnichar* messageUni = message.ToNewUnicode();
    if (!messageUni) return;
    console->LogStringMessage(messageUni);
    nsMemory::Free(messageUni);
  }
#ifndef DEBUG
  else // If JS console reporting failed, print to stderr.
#endif
  {
    char* messageCstr = message.ToNewCString();
    if (!messageCstr) return;
    fprintf(stderr, "%s\n", messageCstr);
    nsMemory::Free(messageCstr);
  }
}

nsresult
nsJAR::RestoreModTime(nsZipItem *aItem, nsIFile *aExtractedFile)
{
  if (!aItem || !aExtractedFile)
    return NS_ERROR_NULL_POINTER;
  
  char *timestr;
  PRTime prtime;
  nsresult rv = NS_OK;
  
  timestr = aItem->GetModTime();
  if (timestr)
  {
    if (PR_SUCCESS == PR_ParseTimeString(timestr, PR_FALSE, &prtime))
    {
    	PRTime conversion = LL_ZERO;
    	PRTime newTime = LL_ZERO;
    	LL_I2L(conversion, PR_USEC_PER_MSEC);
    	LL_DIV(newTime, prtime, conversion);
        // nsIFile needs usecs.
      	rv = aExtractedFile->SetLastModificationDate(newTime);
	}

    JAR_NULLFREE(timestr);
  }

  return rv;
}

nsresult nsJAR::CalculateDigest(nsISignatureVerifier* verifier,
                                const char* aInBuf, PRUint32 aLen,
                                char** digest)
{
  *digest = nsnull;
  nsresult rv;
  
  //-- Calculate the digest
  PRUint32 id;
  rv = verifier->HashBegin(nsISignatureVerifier::SHA1, &id);
  if (NS_FAILED(rv)) return rv;

  rv = verifier->HashUpdate(id, aInBuf, aLen);
  if (NS_FAILED(rv)) return rv;
  
  PRUint32 len;
  unsigned char* rawDigest = (unsigned char*)PR_MALLOC(nsISignatureVerifier::SHA1_LENGTH);
  if (rawDigest == nsnull) return NS_ERROR_OUT_OF_MEMORY;
  rv = verifier->HashEnd(id, &rawDigest, &len, nsISignatureVerifier::SHA1_LENGTH);
  if (NS_FAILED(rv)) { PR_FREEIF(rawDigest); return rv; }

  //-- Encode the digest in base64
  *digest = PL_Base64Encode((char*)rawDigest, len, *digest);
  if (!(*digest)) { PR_FREEIF(rawDigest); return NS_ERROR_OUT_OF_MEMORY; }
  
  PR_FREEIF(rawDigest);
  return NS_OK;
}

//----------------------------------------------
// Debugging functions
//----------------------------------------------
#if 0
PR_STATIC_CALLBACK(PRBool)
PrintManItem(nsHashKey* aKey, void* aData, void* closure)
{
  nsJARManifestItem* manItem = (nsJARManifestItem*)aData;
    if (manItem)
    {
      nsCStringKey* key2 = (nsCStringKey*)aKey;
      char* name = key2->GetString().ToNewCString();
      if (!(PL_strcmp(name, "") == 0))
        printf("%s s=%i\n",name, manItem->status);
    }
    return PR_TRUE;
}
#endif

void nsJAR::DumpMetadata(const char* aMessage)
{
#if 0
  printf("### nsJAR::DumpMetadata at %s ###\n", aMessage);
  if (mPrincipal)
  {
    char* toStr;
    mPrincipal->ToString(&toStr);
    printf("Principal: %s.\n", toStr);
    PR_FREEIF(toStr);
  }
  else
    printf("No Principal. \n");
  mManifestData.Enumerate(PrintManItem);
  printf("\n");
#endif
} 

//----------------------------------------------
// nsJAREnumerator constructor and destructor
//----------------------------------------------
nsJAREnumerator::nsJAREnumerator(nsZipFind *aFind)
: mFind(aFind),
  mCurr(nsnull),
  mIsCurrStale(PR_TRUE)
{
    mArchive = mFind->GetArchive();
    NS_INIT_REFCNT();
}

nsJAREnumerator::~nsJAREnumerator()
{
    mArchive->FindFree(mFind);
}

NS_IMPL_ISUPPORTS(nsJAREnumerator, NS_GET_IID(nsISimpleEnumerator));

//----------------------------------------------
// nsJAREnumerator::HasMoreElements
//----------------------------------------------
NS_IMETHODIMP
nsJAREnumerator::HasMoreElements(PRBool* aResult)
{
    PRInt32 err;

    if (!mFind)
        return NS_ERROR_NOT_INITIALIZED;

    // try to get the next element
    if (mIsCurrStale)
    {
        err = mArchive->FindNext( mFind, &mCurr );
        if (err == ZIP_ERR_FNF)
        {
            *aResult = PR_FALSE;
            return NS_OK;
        }
        if (err != ZIP_OK)
            return NS_ERROR_FAILURE; // no error translation

        mIsCurrStale = PR_FALSE;
    }

    *aResult = PR_TRUE;
    return NS_OK;
}

//----------------------------------------------
// nsJAREnumerator::GetNext
//----------------------------------------------
NS_IMETHODIMP
nsJAREnumerator::GetNext(nsISupports** aResult)
{
    nsresult rv;
    PRBool   bMore;

    // check if the current item is "stale"
    if (mIsCurrStale)
    {
        rv = HasMoreElements( &bMore );
        if (NS_FAILED(rv))
            return rv;
        if (bMore == PR_FALSE)
        {
            *aResult = nsnull;  // null return value indicates no more elements
            return NS_OK;
        }
    }

    // pack into an nsIJARItem
    nsJARItem* jarItem = new nsJARItem();
    if(jarItem)
    {
      NS_ADDREF(jarItem);
      jarItem->Init(mCurr);
      *aResult = jarItem;
      mIsCurrStale = PR_TRUE; // we just gave this one away
      return NS_OK;
    }
    else
      return NS_ERROR_OUT_OF_MEMORY;
}

//-------------------------------------------------
// nsJARItem constructors and destructor
//-------------------------------------------------
nsJARItem::nsJARItem()
{
    NS_INIT_ISUPPORTS();
}

nsJARItem::~nsJARItem()
{
}

NS_IMPL_ISUPPORTS1(nsJARItem, nsIZipEntry);

void nsJARItem::Init(nsZipItem* aZipItem)
{
  mZipItem = aZipItem;
}

//------------------------------------------
// nsJARItem::GetName
//------------------------------------------
NS_IMETHODIMP
nsJARItem::GetName(char * *aName)
{
    char *namedup;

    if ( !aName )
        return NS_ERROR_NULL_POINTER;
    if ( !mZipItem->name )
        return NS_ERROR_FAILURE;

    namedup = PL_strndup( mZipItem->name, mZipItem->namelen );
    if ( !namedup )
        return NS_ERROR_OUT_OF_MEMORY;

    *aName = namedup;
    return NS_OK;
}

//------------------------------------------
// nsJARItem::GetCompression
//------------------------------------------
NS_IMETHODIMP 
nsJARItem::GetCompression(PRUint16 *aCompression)
{
    if (!aCompression)
        return NS_ERROR_NULL_POINTER;
    if (!mZipItem->compression)
        return NS_ERROR_FAILURE;

    *aCompression = mZipItem->compression;
    return NS_OK;
}

//------------------------------------------
// nsJARItem::GetSize
//------------------------------------------
NS_IMETHODIMP 
nsJARItem::GetSize(PRUint32 *aSize)
{
    if (!aSize)
        return NS_ERROR_NULL_POINTER;
    if (!mZipItem->size)
        return NS_ERROR_FAILURE;

    *aSize = mZipItem->size;
    return NS_OK;
}

//------------------------------------------
// nsJARItem::GetRealSize
//------------------------------------------
NS_IMETHODIMP 
nsJARItem::GetRealSize(PRUint32 *aRealsize)
{
    if (!aRealsize)
        return NS_ERROR_NULL_POINTER;
    if (!mZipItem->realsize)
        return NS_ERROR_FAILURE;

    *aRealsize = mZipItem->realsize;
    return NS_OK;
}

//------------------------------------------
// nsJARItem::GetCrc32
//------------------------------------------
NS_IMETHODIMP 
nsJARItem::GetCRC32(PRUint32 *aCrc32)
{
    if (!aCrc32)
        return NS_ERROR_NULL_POINTER;
    if (!mZipItem->crc32)
        return NS_ERROR_FAILURE;

    *aCrc32 = mZipItem->crc32;
    return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsIZipReaderCache

NS_IMPL_THREADSAFE_ISUPPORTS1(nsZipReaderCache, nsIZipReaderCache)

nsZipReaderCache::nsZipReaderCache()
  : mLock(nsnull),
    mZips(16),
#ifdef ZIP_CACHE_HIT_RATE
    mZipCacheLookups(0),
    mZipCacheHits(0),
    mZipCacheFlushes(0),
#endif
    mFreeCount(0)
{
  NS_INIT_REFCNT();
}

NS_IMETHODIMP
nsZipReaderCache::Init(PRUint32 cacheSize)
{
#ifdef DEBUG_warren
  mCacheSize = 1;//cacheSize;   // XXX hack
#else
  mCacheSize = cacheSize;
#endif
  mLock = PR_NewLock();
  return mLock ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

static PRBool PR_CALLBACK
DropZipReaderCache(nsHashKey *aKey, void *aData, void* closure)
{
  nsJAR* zip = (nsJAR*)aData;
  zip->SetZipReaderCache(nsnull);
  return PR_TRUE;
}

nsZipReaderCache::~nsZipReaderCache()
{
  if (mLock)
    PR_DestroyLock(mLock);
  mZips.Enumerate(DropZipReaderCache, nsnull);

#ifdef ZIP_CACHE_HIT_RATE
  printf("nsZipReaderCache size=%d hits=%d lookups=%d rate=%f%% flushes=%d\n",
         mZipCacheHits, mZipCacheLookups, (float)mZipCacheHits / mZipCacheLookups,
         mZipCacheFlushes);
#endif
}

NS_METHOD
nsZipReaderCache::Create(nsISupports *aOuter, REFNSIID aIID, void **aResult)
{
  if (aOuter)
    return NS_ERROR_NO_AGGREGATION;

  nsZipReaderCache* cache = new nsZipReaderCache();
  if (cache == nsnull)
    return NS_ERROR_OUT_OF_MEMORY;
  NS_ADDREF(cache);
  nsresult rv = cache->QueryInterface(aIID, aResult);
  NS_RELEASE(cache);
  return rv;
}

NS_IMETHODIMP
nsZipReaderCache::GetZip(nsIFile* zipFile, nsIZipReader* *result)
{
  nsresult rv;
  nsAutoLock lock(mLock);

#ifdef ZIP_CACHE_HIT_RATE
  mZipCacheLookups++;
#endif

  nsXPIDLCString path;
  rv = zipFile->GetPath(getter_Copies(path));
  if (NS_FAILED(rv)) return rv;

  nsCStringKey key(path);
  nsJAR* zip = (nsJAR*)mZips.Get(&key); // AddRefs
  if (zip) {
#ifdef ZIP_CACHE_HIT_RATE
    mZipCacheHits++;
#endif
    if (zip->GetReleaseTime() != PR_INTERVAL_NO_TIMEOUT) {
      // this was an otherwise-free entry, so decrement our free counter
      NS_ASSERTION(mFreeCount > 0, "mFreeCount screwed up");
      mFreeCount--;
    }
  }
  else {
    zip = new nsJAR();
    if (zip == nsnull)
        return NS_ERROR_OUT_OF_MEMORY;
    NS_ADDREF(zip);
    zip->SetZipReaderCache(this);

    rv = zip->Init(zipFile);
    if (NS_FAILED(rv)) {
      NS_RELEASE(zip);
      return rv;
    }
    rv = zip->Open();
    if (NS_FAILED(rv)) {
      NS_RELEASE(zip);
      return rv;
    }

    PRBool collision = mZips.Put(&key, zip); // AddRefs to 2
    NS_ASSERTION(!collision, "horked");
  }
  *result = zip;
  return rv;
}

static PRBool PR_CALLBACK
FindOldestZip(nsHashKey *aKey, void *aData, void* closure)
{
  nsJAR** oldestPtr = (nsJAR**)closure;
  nsJAR* oldest = *oldestPtr;
  nsJAR* current = (nsJAR*)aData;
  PRIntervalTime currentReleaseTime = current->GetReleaseTime();
  if (currentReleaseTime != PR_INTERVAL_NO_TIMEOUT) {
    if (oldest == nsnull ||
        currentReleaseTime < oldest->GetReleaseTime()) {
      *oldestPtr = current;
    }    
  }
  return PR_TRUE;
}

nsresult
nsZipReaderCache::ReleaseZip(nsJAR* zip)
{
  nsresult rv;
  nsAutoLock lock(mLock);

  mFreeCount++;
  if (mZips.Count() <= mCacheSize)
    return NS_OK;

  nsJAR* oldest = nsnull;
  if (mFreeCount == 1) {
    // then this is our guy -- no need to search for the oldest
    oldest = zip;
  }
  else {
    mZips.Enumerate(FindOldestZip, &oldest);
  }
  NS_ASSERTION(oldest, "wacked");

#ifdef ZIP_CACHE_HIT_RATE
    mZipCacheFlushes++;
#endif

  // remove from hashtable
  nsCOMPtr<nsIFile> zipFile;
  rv = oldest->GetFile(getter_AddRefs(zipFile));
  if (NS_FAILED(rv)) return rv;

  nsXPIDLCString path;
  rv = zipFile->GetPath(getter_Copies(path));
  if (NS_FAILED(rv)) return rv;

  nsCStringKey key(path);
  PRBool removed = mZips.Remove(&key);  // Releases
  NS_ASSERTION(removed, "botched");

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
