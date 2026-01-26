/**
 * POSIX compatibility stubs for ESP-IDF 5.x + Arduino 3.x
 *
 * These functions are declared in newlib headers but not implemented
 * in the current ESP-IDF configuration. We provide minimal implementations
 * that work with ESP32's VFS layer.
 */

#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

// Only compile these stubs if they're not already provided
#if defined(ESP_PLATFORM) && !defined(POSIX_COMPAT_PROVIDED)

extern "C" {

/**
 * Check file accessibility
 * Simplified implementation using stat()
 */
int access(const char *path, int mode) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;  // File doesn't exist or can't be accessed
    }

    // For ESP32, if stat() succeeds, we assume the file is accessible
    // Mode checking (R_OK, W_OK, X_OK) is simplified since ESP32
    // filesystems don't have traditional Unix permissions
    return 0;
}

/**
 * Remove a directory
 * Uses the VFS unlink function which works for empty directories on LittleFS
 */
int rmdir(const char *path) {
    // On ESP32 with LittleFS/SPIFFS, we can use unlink for empty directories
    // or return an error if the directory is not empty
    return unlink(path);
}

}  // extern "C"

#endif  // ESP_PLATFORM
