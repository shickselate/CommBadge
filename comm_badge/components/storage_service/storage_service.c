/*
 * storage_service.c — Wear-levelled FAT filesystem on the "audio" flash partition.
 *
 * Uses esp_vfs_fat_spiflash_mount_rw_wl to mount the partition labelled "audio"
 * (defined in partitions.csv) at the VFS path /audio.  Once mounted, any code
 * can use standard stdio (fopen, fwrite, fread, fclose) on paths under /audio/.
 *
 * The wear-levelling layer ensures flash longevity across repeated record cycles.
 */

#include "storage_service.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "esp_log.h"
#include <sys/statvfs.h>

static const char *TAG = "storage_service";

#define MOUNT_POINT     "/audio"
#define PARTITION_LABEL "audio"
#define RECORDING_PATH  MOUNT_POINT "/recording.wav"

static wl_handle_t     s_wl_handle  = WL_INVALID_HANDLE;
static bool            s_mounted    = false;

esp_err_t storage_service_init(void)
{
    if (s_mounted) {
        ESP_LOGW(TAG, "Already mounted");
        return ESP_ERR_INVALID_STATE;
    }

    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = true,   /* Format on first boot or after corruption */
        .max_files              = 4,
        .allocation_unit_size   = 4096,
    };

    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(
        MOUNT_POINT,
        PARTITION_LABEL,
        &mount_cfg,
        &s_wl_handle
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FAT filesystem: %s", esp_err_to_name(err));
        return err;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "Mounted FAT on partition \"%s\" at %s", PARTITION_LABEL, MOUNT_POINT);
    ESP_LOGI(TAG, "Free space: %llu bytes", storage_get_free_bytes());
    return ESP_OK;
}

const char *storage_get_recording_path(void)
{
    return RECORDING_PATH;
}

uint64_t storage_get_free_bytes(void)
{
    if (!s_mounted) return 0;

    struct statvfs st;
    if (statvfs(MOUNT_POINT, &st) != 0) {
        ESP_LOGW(TAG, "statvfs failed");
        return 0;
    }
    return (uint64_t)st.f_bsize * (uint64_t)st.f_bfree;
}
