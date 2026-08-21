#include <stdint.h>
#include <stdbool.h>
#include "limine.h"

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_address_request exec_addr_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

/* Expose request responses to other boot files */
struct limine_framebuffer_response *boot_get_framebuffer_response(void) {
    return framebuffer_request.response;
}
struct limine_hhdm_response *boot_get_hhdm_response(void) {
    return hhdm_request.response;
}
struct limine_executable_address_response *boot_get_exec_addr_response(void) {
    return exec_addr_request.response;
}
struct limine_memmap_response *boot_get_memmap_response(void) {
    return memmap_request.response;
}
struct limine_module_response *boot_get_module_response(void) {
    return module_request.response;
}
struct limine_rsdp_response *boot_get_rsdp_response(void) {
    return rsdp_request.response;
}
const volatile uint64_t *boot_get_base_revision(void) {
    return limine_base_revision;
}
