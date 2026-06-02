#pragma once

#include "Archive.h"
#include "Platform/Paths.h"
#include <fstream>
#include <string>
#include <iostream>

class FWindowsBinWriter : public FArchive
{
private:
	std::ofstream FileStream;

public:
	FWindowsBinWriter(const std::string& FilePath)
	{
		bIsSaving = true; // 나는 '쓰기' 전용이다!
		FileStream.open(FPaths::ToWide(FilePath), std::ios::binary);
	}

	~FWindowsBinWriter() override
	{
		if (FileStream.is_open()) FileStream.close();
	}

	// 파일이 정상적으로 열렸는지 확인
	bool IsValid() const { return FileStream.is_open() && FileStream.good(); }
	bool SupportsPosition() const override { return true; }
	size_t Tell() const override { return FileStream.is_open() ? static_cast<size_t>(const_cast<std::ofstream&>(FileStream).tellp()) : 0; }
	size_t TotalSize() const override { return Tell(); }

	void Serialize(void* Data, size_t Num) override
	{
		if (FileStream.is_open() && Num > 0)
		{
			// 하드 디스크에 데이터를 씁니다.
			FileStream.write(static_cast<const char*>(Data), Num);
		}
	}
};

class FWindowsBinReader : public FArchive
{
private:
	std::ifstream FileStream;
	size_t FileSize = 0;

public:
	FWindowsBinReader(const std::string& FilePath)
	{
		bIsLoading = true; // 나는 '읽기' 전용이다!
		FileStream.open(FPaths::ToWide(FilePath), std::ios::binary);
		if (FileStream.is_open())
		{
			FileStream.seekg(0, std::ios::end);
			FileSize = static_cast<size_t>(FileStream.tellg());
			FileStream.seekg(0, std::ios::beg);
		}
	}

	~FWindowsBinReader() override
	{
		if (FileStream.is_open()) FileStream.close();
	}

	bool IsValid() const { return FileStream.is_open() && FileStream.good(); }
	bool SupportsPosition() const override { return true; }
	size_t Tell() const override { return FileStream.is_open() ? static_cast<size_t>(const_cast<std::ifstream&>(FileStream).tellg()) : 0; }
	size_t TotalSize() const override { return FileSize; }

	void Serialize(void* Data, size_t Num) override
	{
		if (FileStream.is_open() && Num > 0)
		{
			// 하드 디스크에서 데이터를 읽어옵니다.
			FileStream.read(static_cast<char*>(Data), Num);
		}
	}
};
