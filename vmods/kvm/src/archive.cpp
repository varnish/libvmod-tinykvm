#include "program_instance.hpp"

#include "curl_fetch.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <cstring>
#include <tinykvm/util/elf.hpp>

namespace kvm
{
	static constexpr bool VERBOSE_ARCHIVE = true;
	static constexpr bool ALL_FORMATS = false;

	static std::vector<uint8_t> to_vector(struct archive *a, struct archive_entry *entry)
	{
		std::vector<uint8_t> result;
		result.resize(archive_entry_size(entry));
		const auto bytes = archive_read_data(a, result.data(), result.size());
		if (bytes != (long)result.size())
			throw std::runtime_error("Mismatch when reading entry data from archive");
		return result;
	}
	static bool endsWith (std::string const& str, std::string const& ending) {
		if (str.length() >= ending.length()) {
			return (0 == str.compare (str.length() - ending.length(), ending.length(), ending));
		} else {
			return false;
		}
	}

	void extract_programs_to(kvm::ProgramInstance& program, const char *chunk, size_t chunk_size)
	{
		if (chunk_size < 64)
			throw std::runtime_error("Binary or archive was too small (< 64 bytes)");

		// 1. Check if the chunk is an ELF binary
		auto* elf = (Elf64_Ehdr *)chunk;
		if (tinykvm::validate_header(elf))
		{
			// If it's a single binary, use the same binary for both request and storage
			program.request_binary = std::vector<uint8_t>(chunk, chunk + chunk_size);
			program.storage_binary = program.request_binary;
			return;
		}

		// 2. Attempt libarchive, with all formats supported
		// Use entry named 'storage' for storage binary, and any
		// other entry as the request binary.
		struct archive *a = archive_read_new();
		if constexpr (ALL_FORMATS) {
			archive_read_support_format_all(a);
		} else {
			archive_read_support_format_tar(a);
			archive_read_support_filter_xz(a);
		}
		if (archive_read_open_memory(a, chunk, chunk_size) != ARCHIVE_OK)
		{
			fprintf(stderr, "%s\n", archive_error_string(a));
			throw std::runtime_error("Could not decipher the program type, neither archive nor binary");
		}
		try { while (true)
		{
			struct archive_entry *entry = nullptr;
			const int r = archive_read_next_header(a, &entry);
			if (r == ARCHIVE_EOF)
				break;

			if (r != ARCHIVE_OK) {
				fprintf(stderr, "%s\n", archive_error_string(a));
				throw std::runtime_error("Archive *entry* not OK");
			}

			if (archive_entry_size(entry) > 0)
			{
				const std::string name {archive_entry_pathname(entry)};
				if (endsWith(name, "storage"))
				{
					program.storage_binary = to_vector(a, entry);
					if constexpr (VERBOSE_ARCHIVE) {
						printf("Found '%s' size=%zu, used as storage program\n",
							name.c_str(), program.storage_binary.size());
					}
				}
				else
				{
					if (!program.request_binary.empty())
						throw std::runtime_error("Confusing archive with more than one request program");

					program.request_binary = to_vector(a, entry);
					if constexpr (VERBOSE_ARCHIVE) {
						printf("Found '%s' size=%zu, used as request program\n",
							name.c_str(), program.request_binary.size());
					}
				}
			}
		} } catch (...) {
			archive_read_free(a);
			throw;
		}
	}
}