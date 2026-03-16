///////////////////////////////////////////////////////////////////////////////
//         Gigi Rapid Graphics Prototyping and Code Generation Suite         //
//        Copyright (c) 2024 Electronic Arts Inc. All rights reserved.       //
///////////////////////////////////////////////////////////////////////////////

#include "FileCache.h"

#include <filesystem>

std::string CanonifyFileName(const char* fileName)
{
    std::string ret = std::filesystem::weakly_canonical(fileName).string();

    // Don't make wsl paths lower case, because they are case sensitive!
    if (ret.length() > 0 && ret[0] != '\\')
        std::transform(ret.begin(), ret.end(), ret.begin(), [](unsigned char c) { return std::tolower(c); });

    return ret;
}

FileCache::File& FileCache::Get(const char* fileName)
{
    std::string s = CanonifyFileName(fileName);

	// If we don't have an entry for this file, create one
	// Add an extra null character at the end for text files.
	// We'll report the size as size-1 to account for this.
	if (m_cache.count(s) == 0)
	{
		File newFile;
        newFile.fileName = s;

        std::string longFileName;
        if (s.c_str() && fileName[0] == '\\' && fileName[1] == '\\')
            longFileName = std::string("\\\\?\\UNC") + &s[1];
        else
            longFileName = std::string("\\\\?\\") + s;

		FILE* file = nullptr;
		auto e = fopen_s(&file, longFileName.c_str(), "rb");
		if (file)
		{
			fseek(file, 0, SEEK_END);
			newFile.bytes.resize(ftell(file) + 1);
			fseek(file, 0, SEEK_SET);
			fread(newFile.bytes.data(), 1, newFile.bytes.size() - 1, file);
			newFile.bytes[newFile.bytes.size() - 1] = 0;
			fclose(file);
		}
        else
        {
            char err_msg[4096];
            strerror_s(err_msg, sizeof(err_msg), e); // Use strerror_s for security
            int ijkl = 0;
        }

		m_cache[s] = newFile;
	}

	// return the entry for this file
	return m_cache[s];
}