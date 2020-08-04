#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>

inline uint32_t read_magic(FILE* pmem, long offset)
{
	if (fseek(pmem, offset, SEEK_SET) < 0) {
		fprintf(stderr, "Could not seek to offset: %lx\n", offset);
		exit(1);
	}
	uint32_t magic;
	const size_t MSIZE = sizeof(magic);
	if (fread(&magic, MSIZE, 1, pmem) != 1) {
		fprintf(stderr, "Unable to read magic value at %#lx\n", offset);
		exit(1);
	}
	return magic;
}

inline uint64_t read64(FILE* pmem, long offset)
{
	if (fseek(pmem, offset, SEEK_SET) < 0) {
		fprintf(stderr, "Could not seek to offset: %lx\n", offset);
		exit(1);
	}
	uint64_t value;
	const size_t MSIZE = sizeof(value);
	if (fread(&value, MSIZE, 1, pmem) != 1) {
		fprintf(stderr, "Unable to read 64-bit value at %#lx\n", offset);
		return 0;
	}
	return value;
}

inline std::string readstr(FILE* pmem, long offset)
{
	char buffer[128] = {0};
	uint64_t addr = read64(pmem, offset);
	if (addr != 0) {
		if (fseek(pmem, addr, SEEK_SET) < 0) {
			fprintf(stderr, "Could not seek to address: %lx\n", addr);
			exit(1);
		}
		if (fgets(buffer, sizeof(buffer), pmem))
			return std::string(buffer);
	}
	return "";
}

int main(int argc, char** argv)
{
	int pid = 0;
	long offset = 0;
	if (argc < 3) {
		fprintf(stderr, "Usage: %s PID 0xoffset\n", argv[0]);
		exit(1);
	}

	sscanf(argv[1], "%d", &pid);
	sscanf(argv[2], "%lx", &offset);

	if (pid == 0) {
		fprintf(stderr, "Invalid PID: %s\n", argv[1]);
		exit(1);
	}
	if (offset == 0) {
		fprintf(stderr, "Invalid memory offset: %s\n", argv[2]);
		exit(1);
	}

	char procbuffer[128];
	snprintf(procbuffer, sizeof(procbuffer),
		"/proc/%d/mem", pid);
	FILE* pmem = fopen(procbuffer, "r");
	if (!pmem) {
		fprintf(stderr, "Could not process memory: %s\n", procbuffer);
		exit(1);
	}

	const uint32_t magic = read_magic(pmem, offset);
	printf("The magic value at %lx is %x\n", offset, magic);

	for (int off = 4; off < 256; off += 4) {
		auto string = readstr(pmem, offset + off);
		if (!string.empty()) {
			printf("String at offset %d: %s\n",
				off, string.c_str());
		}
	}

	fclose(pmem);
	return 0;
}
