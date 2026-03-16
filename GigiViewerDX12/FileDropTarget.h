///////////////////////////////////////////////////////////////////////////////
//         Gigi Rapid Graphics Prototyping and Code Generation Suite         //
//        Copyright (c) 2026 Electronic Arts Inc. All rights reserved.       //
///////////////////////////////////////////////////////////////////////////////

#pragma once

// Forward declarations of functions defined in main.cpp / GigiCompilerLib
bool LoadGGFile(const char* fileName, bool preserveState, bool addToRecentFiles);
std::string FromWideString(const wchar_t* string);

class FileDropTarget : public IDropTarget
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (!ppvObject)
            return E_POINTER;

        if (riid == IID_IUnknown || riid == IID_IDropTarget)
        {
            *ppvObject = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }

        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return (ULONG)InterlockedIncrement(&m_refCount);
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG refCount = (ULONG)InterlockedDecrement(&m_refCount);
        if (refCount == 0)
            delete this;
        return refCount;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* pDataObj, DWORD /*grfKeyState*/, POINTL /*pt*/, DWORD* pdwEffect) override
    {
        if (!pdwEffect)
            return E_POINTER;

        *pdwEffect = HasDropFiles(pDataObj) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD /*grfKeyState*/, POINTL /*pt*/, DWORD* pdwEffect) override
    {
        if (!pdwEffect)
            return E_POINTER;

        *pdwEffect = DROPEFFECT_COPY;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* pDataObj, DWORD /*grfKeyState*/, POINTL /*pt*/, DWORD* pdwEffect) override
    {
        if (!pdwEffect)
            return E_POINTER;

        *pdwEffect = DROPEFFECT_NONE;

        if (!pDataObj)
            return E_INVALIDARG;

        bool loaded = false;

        // Primary path for file-system drags.
        HRESULT hr = LoadFromHDrop(pDataObj, loaded);

        // Explorer may fail to render CF_HDROP for very long paths. Fall back to PIDLs.
        if (!loaded && (hr == E_NOT_SUFFICIENT_BUFFER || hr == DV_E_FORMATETC || hr == DV_E_TYMED))
            hr = LoadFromShellIdList(pDataObj, loaded);

        *pdwEffect = loaded ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return loaded ? S_OK : hr;
    }

private:
    static HRESULT LoadFromHDrop(IDataObject* pDataObj, bool& loaded)
    {
        loaded = false;

        FORMATETC format = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM medium = {};
        HRESULT hr = pDataObj->GetData(&format, &medium);
        if (FAILED(hr))
            return hr;

        HDROP hDrop = (HDROP)medium.hGlobal;

        UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        if (fileCount > 0)
        {
            UINT fileNameLen = DragQueryFileW(hDrop, 0, nullptr, 0);
            std::vector<wchar_t> fileName(fileNameLen + 1, L'\0');
            if (DragQueryFileW(hDrop, 0, fileName.data(), fileNameLen + 1) > 0)
                loaded = LoadGGFile(FromWideString(fileName.data()).c_str(), false, true);
        }

        ReleaseStgMedium(&medium);
        return loaded ? S_OK : E_FAIL;
    }

    static HRESULT LoadFromShellIdList(IDataObject* pDataObj, bool& loaded)
    {
        loaded = false;

        CLIPFORMAT shellIdListFormat = (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_SHELLIDLIST);
        if (!shellIdListFormat)
            return E_FAIL;

        FORMATETC format = { shellIdListFormat, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM medium = {};
        HRESULT hr = pDataObj->GetData(&format, &medium);
        if (FAILED(hr))
            return hr;

        const CIDA* cida = (const CIDA*)GlobalLock(medium.hGlobal);
        if (!cida)
        {
            ReleaseStgMedium(&medium);
            return E_FAIL;
        }

        const BYTE* base = (const BYTE*)cida;
        if (cida->cidl > 0)
        {
            PCIDLIST_ABSOLUTE folder = (PCIDLIST_ABSOLUTE)(base + cida->aoffset[0]);
            PCUIDLIST_RELATIVE child = (PCUIDLIST_RELATIVE)(base + cida->aoffset[1]);
            PIDLIST_ABSOLUTE fullPidl = ILCombine(folder, child);
            if (fullPidl)
            {
                std::vector<wchar_t> fileName(32768, L'\0');
                if (SHGetPathFromIDListEx(fullPidl, fileName.data(), (UINT)fileName.size(), GPFIDL_DEFAULT))
                    loaded = LoadGGFile(FromWideString(fileName.data()).c_str(), false, true);

                CoTaskMemFree(fullPidl);
            }
        }

        GlobalUnlock(medium.hGlobal);
        ReleaseStgMedium(&medium);

        return loaded ? S_OK : E_FAIL;
    }

    static bool HasDropFiles(IDataObject* pDataObj)
    {
        if (!pDataObj)
            return false;

        FORMATETC format = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        if (pDataObj->QueryGetData(&format) == S_OK)
            return true;

        CLIPFORMAT shellIdListFormat = (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_SHELLIDLIST);
        if (!shellIdListFormat)
            return false;

        FORMATETC shellIdList = { shellIdListFormat, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        return pDataObj->QueryGetData(&shellIdList) == S_OK;
    }

    volatile LONG m_refCount = 1;
};
