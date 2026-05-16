/**
 * Simple C test for libk1520core.so
 * Helps diagnose why k1520_create() fails
 */

#include <stdio.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdbool.h>

typedef void* K1520Handle;

typedef K1520Handle (*k1520_create_fn)(int type);
typedef void (*k1520_destroy_fn)(K1520Handle h);
typedef void (*k1520_power_on_fn)(K1520Handle h);
typedef int (*k1520_run_fn)(K1520Handle h, int max_cycles);

int main() {
    printf("K1520 Library Test\n");
    printf("==================\n\n");
    
    // Load library
    printf("Loading libk1520core.so... ");
    void* lib = dlopen("./build/libk1520core.so", RTLD_LAZY);
    if (!lib) {
        printf("FAILED\n");
        printf("Error: %s\n", dlerror());
        return 1;
    }
    printf("OK\n\n");
    
    // Get function pointers
    printf("Looking up symbols...\n");
    
    k1520_create_fn k1520_create = (k1520_create_fn)dlsym(lib, "k1520_create");
    if (!k1520_create) {
        printf("ERROR: k1520_create not found: %s\n", dlerror());
        dlclose(lib);
        return 1;
    }
    printf("  k1520_create: OK\n");
    
    k1520_destroy_fn k1520_destroy = (k1520_destroy_fn)dlsym(lib, "k1520_destroy");
    printf("  k1520_destroy: %s\n", k1520_destroy ? "OK" : "NOT FOUND");
    
    k1520_power_on_fn k1520_power_on = (k1520_power_on_fn)dlsym(lib, "k1520_power_on");
    printf("  k1520_power_on: %s\n", k1520_power_on ? "OK" : "NOT FOUND");
    
    k1520_run_fn k1520_run = (k1520_run_fn)dlsym(lib, "k1520_run");
    printf("  k1520_run: %s\n", k1520_run ? "OK" : "NOT FOUND");
    
    printf("\n");
    
    // Create machine
    printf("Creating A5120 machine (type=0)... ");
    fflush(stdout);
    
    K1520Handle h = k1520_create(0);  // K1520_MACHINE_A5120 = 0
    
    if (!h) {
        printf("FAILED\n");
        printf("Error: k1520_create returned NULL\n");
        dlclose(lib);
        return 1;
    }
    printf("OK (handle=%p)\n", h);
    
    // Power on
    printf("Powering on... ");
    fflush(stdout);
    k1520_power_on(h);
    printf("OK\n");
    
    // Run a few cycles
    printf("Running 1000 cycles... ");
    fflush(stdout);
    int cycles = k1520_run(h, 1000);
    printf("OK (%d cycles)\n", cycles);
    
    // Cleanup
    printf("Destroying machine... ");
    k1520_destroy(h);
    printf("OK\n");
    
    dlclose(lib);
    
    printf("\nAll tests passed!\n");
    return 0;
}
