#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <xf86drm.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include "libpscnv.h"

#define TEST_GEM_SIZE 0x1234

void
test_gem(int fd, uint32_t expect_cookie, uint32_t gem_handle)
{
	int ret;
	uint32_t actual_cookie;
	
	ret = pscnv_gem_info(fd, gem_handle, &actual_cookie, NULL, NULL, NULL, NULL, NULL);
	
	if (expect_cookie && ret) {
		printf("  test_gem: expected cookie %x, but gem_info failed\n", expect_cookie);
	}
	
	if (!expect_cookie && !ret) {
		printf("  test_gem: gem should not be here, but gem_info returned cookie %x", actual_cookie);
	}
	
	if (expect_cookie && !ret && expect_cookie != actual_cookie) {
		printf("  test_gem: gem is there, but expected cookie %x, got: %x", expect_cookie, actual_cookie);
	}
}

void
test_gem_new(int fd, uint32_t cookie, uint32_t *gem_handle, uint64_t *map_handle)
{
	int ret;
	
	ret = pscnv_gem_new(fd, cookie, 0 /*flags */, 0 /* tile flags */,
	                    TEST_GEM_SIZE, NULL /* user */, gem_handle,
			    map_handle);
	if (ret) {
		printf("  new: failed ret = %d\n", ret);
	}
	
	test_gem(fd, cookie, *gem_handle);
}

void
test_gem_close(int fd, uint32_t gem_handle)
{
	int ret;
	
	ret = pscnv_gem_close(fd, gem_handle);
	if (ret) {
		printf("  close: failed ret = %d\n", ret);
	}
}

void
test_vspace_new(int fd, uint32_t *vid)
{
	int ret;
	
	ret = pscnv_vspace_new(fd, vid);
	if (ret) {
		printf("  vspace new: failed ret = %d\n", ret);
	}
}

void
test_vspace_map(int fd, uint32_t vid, uint32_t gem_handle, uint64_t *vm_addr)
{
	int ret;
	
	ret = pscnv_vspace_map(fd, vid, gem_handle, 0x20000000, 1ull << 40, 0, 0, vm_addr);
	if (ret) {
		printf("  vspace map: failed ret = %d\n", ret);
	}
}

void
test_vspace_unmap(int fd, uint32_t vid, uint64_t vm_addr)
{
	int ret;
	
	ret = pscnv_vspace_unmap(fd, vid, vm_addr);
	if (ret) {
		printf("  vspace unmap: failed ret = %d\n", ret);
	}
}

void
test_vspace_free(int fd, uint32_t vid)
{
	int ret;
	
	ret = pscnv_vspace_free(fd, vid);
	if (ret) {
		printf("  vspace close: failed ret = %d\n", ret);
	}
}

void
test_chan_new(int fd, uint32_t vid, uint32_t *cid, uint64_t *map_handle)
{
	int ret;
	
	ret = pscnv_chan_new(fd, vid, cid, map_handle);
	if (ret) {
		printf("  chan new: failed ret = %d\n", ret);
	}
}

void
test_chan_free(int fd, uint32_t cid)
{
	int ret;
	
	ret = pscnv_chan_free(fd, cid);
	if (ret) {
		printf("  chan free: failed ret= %d\n", ret);
	}
	
}

void
test_mmap(int fd, uint64_t mmap_handle, void **mem)
{
	volatile uint32_t *my_mem;
	
	my_mem = mmap(NULL, TEST_GEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mmap_handle);
	
	if (my_mem == MAP_FAILED) {
		perror("  mmap");
		return;
	}
	
	*my_mem = 0x42424242;
	
	if (*my_mem != 0x42424242) {
		printf("  mmap: reading/ writing to memory failed\n");
	}
	
	*mem = (void*) my_mem;
}

void
test_munmap(void *mem)
{
	uint32_t *my_mem = mem;
	
	if (*my_mem != 0x42424242) {
		printf("  munmap: could not read the magic number, got %08x\n", *my_mem);
	}
	
	munmap(mem, TEST_GEM_SIZE);
}


void
easy_case(int fd)
{
	uint32_t gem_handle;
	
	printf("== easy case: gem_new and gem_close immediatly\n");
	test_gem_new(fd, 0x1111, &gem_handle, NULL);
	test_gem_close(fd, gem_handle);
	test_gem(fd, 0, gem_handle);
}

void
vspace_case1(int fd)
{
	uint32_t gem_handle;
	uint32_t vid;
	uint64_t vm_addr;
	
	printf("== vspace case 1: vspace_new, gem_new, map, unmap, gem_close, vspace_free\n");
	test_vspace_new(fd, &vid);
	test_gem_new(fd, 0x2222, &gem_handle, NULL);
	test_vspace_map(fd, vid, gem_handle, &vm_addr);
	test_vspace_unmap(fd, vid, vm_addr);
	test_gem_close(fd, gem_handle);
	test_gem(fd, 0, gem_handle);
	test_vspace_free(fd, vid);
}

void
vspace_case2(int fd)
{
	uint32_t gem_handle;
	uint32_t vid;
	uint64_t vm_addr;
	
	printf("== vspace case 2: gem_new, vspace_new, map, vspace_free, gem_close\n");
	test_gem_new(fd, 0x3333, &gem_handle, NULL);
	test_vspace_new(fd, &vid);
	test_vspace_map(fd, vid, gem_handle, &vm_addr);
	test_vspace_free(fd, vid); // gem should be unmapped, but not free'd
	test_gem(fd, 0x3333, gem_handle);
	test_gem_close(fd, gem_handle);
	test_gem(fd, 0, gem_handle);
}

void
vspace_case3(int fd)
{
	uint32_t gem_handle;
	uint32_t vid1, vid2;
	uint64_t vm1_addr, vm2_addr;
	
	printf("== vspace case 3: gem_new, 2x vspace_new, 2x map, gem_close, vspace_free (1st), unmap, vspace_free(2nd)\n");
	test_gem_new(fd, 0x4444, &gem_handle, NULL);
	test_vspace_new(fd, &vid1);
	test_vspace_new(fd, &vid2);
	test_vspace_map(fd, vid1, gem_handle, &vm1_addr);
	test_vspace_map(fd, vid2, gem_handle, &vm2_addr);
	test_gem_close(fd, gem_handle);
	test_gem(fd, 0, gem_handle); // gem already free'd, bo should stay
	test_vspace_free(fd, vid1);
	test_vspace_unmap(fd, vid2, vm2_addr);
	test_vspace_free(fd, vid2); // only free bo here
}

void
chan_case(int fd)
{
	uint32_t vid, cid;
	
	printf("== chan case: vspace_new, chan_new, chan_free, vspace_free\n");
	test_vspace_new(fd, &vid);
	test_chan_new(fd, vid, &cid, NULL);
	test_chan_free(fd, cid);
	test_vspace_free(fd, vid);
}

void
mmap_case(int fd)
{
	uint32_t gem_handle;
	uint64_t mmap_handle;
	void *mem;
	
	printf("== mmap case: gem_new, mmap, gem_close, unmap\n");
	test_gem_new(fd, 0x5555, &gem_handle, &mmap_handle);
	test_mmap(fd, mmap_handle, &mem);
	test_gem_close(fd, gem_handle);
	test_gem(fd, 0, gem_handle);
	test_munmap(mem);
}

int
main()
{
	int fd;
	int result;
	int ret;
	
	uint64_t gem_handle_map;
	uint32_t gem_handle;
	
        fd = drmOpen("pscnv", 0);

	if (fd == -1) {
		printf("failed to open DRM device\n");
		return 1;
	}
	
	easy_case(fd);
	vspace_case1(fd);
	vspace_case2(fd);
	vspace_case3(fd);
	chan_case(fd);
	mmap_case(fd);

        close (fd);
        
        return 0;
}