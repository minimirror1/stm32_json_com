/*
 * device_hal.h
 *
 *  Created on: Dec 31, 2025
 *      Author: AI Assistant
 *
 *  Application Hardware Abstraction Layer
 *  - Defines interface between JSON communication library and application implementation
 *  - All App_* functions are __weak stubs that developers override
 *
 *  ============================================================================
 *  DESIGN PRINCIPLE:
 *  ============================================================================
 *  - App_* functions handle PURE APPLICATION LOGIC only
 *  - NO communication parsing or response formatting in App_* functions
 *  - Communication layer (json_com.c) handles all JSON parsing/response building
 *  ============================================================================
 */

#ifndef INC_DEVICE_HAL_H_
#define INC_DEVICE_HAL_H_

#include <stdint.h>
#include <stdbool.h>

/*******************************************************************************
 * Constants
 ******************************************************************************/
#define APP_NAME_MAX_LEN      64
#define APP_PATH_MAX_LEN      128
#define APP_CONTENT_MAX_LEN   512
#define APP_MAX_FILES         64    /* Maximum files returned by App_GetFiles */
#define APP_MAX_DEPTH         10    /* Maximum folder depth (0=root, 1, 2, ... 9) */
#define APP_MAX_MOTORS        32    /* Maximum motors */
#define APP_MOTOR_TYPE_LEN    16    /* Motor type string length */
#define APP_MOTOR_STATUS_LEN  16    /* Motor status string length */

/*******************************************************************************
 * Data Structures (Pure application data - no communication formatting)
 ******************************************************************************/

/**
 * @brief File/folder information for file system operations
 * 
 * Tree structure is represented by depth and parent_index:
 * - depth: 0 = root level, 1 = first subfolder, 2 = second subfolder (max APP_MAX_DEPTH-1)
 * - parent_index: index of parent folder in the array (-1 for root items)
 * 
 * Items MUST be ordered: parent folders must appear before their children.
 * 
 * Example for structure:
 *   Error/
 *     err_lv.ini
 *   Log/
 *     BOOT.TXT
 * 
 * Array representation:
 *   [0] name="Error",      depth=0, parent_index=-1, is_directory=true
 *   [1] name="err_lv.ini", depth=1, parent_index=0,  is_directory=false
 *   [2] name="Log",        depth=0, parent_index=-1, is_directory=true
 *   [3] name="BOOT.TXT",   depth=1, parent_index=2,  is_directory=false
 */
typedef struct {
    char name[APP_NAME_MAX_LEN];      /* File/folder name */
    char path[APP_PATH_MAX_LEN];      /* Full path */
    bool is_directory;                 /* true if directory, false if file */
    uint32_t size;                     /* File size in bytes (0 for directories) */
    uint8_t depth;                     /* Folder depth: 0=root, 1=subfolder, 2=sub-subfolder */
    int16_t parent_index;              /* Index of parent folder (-1 for root items) */
} AppFileInfo;

/**
 * @brief Full motor information for get_motors command
 * 
 * Contains complete motor configuration and current state.
 * Used for initial motor list loading in GUI.
 */
typedef struct {
    uint8_t id;                           /* Motor unique ID */
    uint8_t group_id;                     /* Group ID (for DisplayId = GroupId-SubId) */
    uint8_t sub_id;                       /* Sub ID within group */
    char type[APP_MOTOR_TYPE_LEN];        /* Type: "Servo", "DC", "Stepper" */
    char status[APP_MOTOR_STATUS_LEN];    /* Status: "Normal", "Error" */
    float position;                       /* Current position (e.g., 0~180 for servo) */
    float velocity;                       /* Current velocity */
} AppMotorInfo;

/**
 * @brief Motor state for get_motor_state command (polling)
 * 
 * Contains only runtime state (no configuration).
 * Used for periodic state updates in GUI.
 */
typedef struct {
    uint8_t id;                           /* Motor unique ID */
    char status[APP_MOTOR_STATUS_LEN];    /* Status: "Normal", "Error" */
    float position;                       /* Current position */
    float velocity;                       /* Current velocity */
} AppMotorState;

/*******************************************************************************
 * Application Function Declarations (__weak stubs)
 *
 * Return values:
 *   - bool functions: true = success, false = failure/not implemented
 *   - int functions:  >= 0 = success (count), -1 = failure/not implemented
 ******************************************************************************/

/**
 * @brief Ping check - verify device is responsive
 * @return true if device is ready, false otherwise
 *
 * @example
 *   bool App_Ping(void) {
 *       return true;  // Device is alive
 *   }
 */
bool App_Ping(void);

/**
 * @brief Execute move command
 * @param device_id Target device ID
 * @return true on success, false on failure
 *
 * @example
 *   bool App_Move(uint8_t device_id) {
 *       Motor_MoveToPosition(device_id, 100);
 *       return true;
 *   }
 */
bool App_Move(uint8_t device_id);

/**
 * @brief Start motion sequence
 * @param device_id Target device ID
 * @return true on success, false on failure
 *
 * @example
 *   bool App_MotionStart(uint8_t device_id) {
 *       Motion_Start(device_id);
 *       return true;
 *   }
 */
bool App_MotionStart(uint8_t device_id);

/**
 * @brief Stop motion sequence
 * @param device_id Target device ID
 * @return true on success, false on failure
 */
bool App_MotionStop(uint8_t device_id);

/**
 * @brief Pause motion sequence
 * @param device_id Target device ID
 * @return true on success, false on failure
 */
bool App_MotionPause(uint8_t device_id);

/**
 * @brief Get file/folder list from storage
 * @param out_files Output array of file info structures
 * @param max_count Maximum number of files to return
 * @return Number of files (>= 0) on success, -1 on failure
 *
 * @example
 *   int App_GetFiles(AppFileInfo *out_files, uint16_t max_count) {
 *       int count = 0;
 *       // Read SD card directory...
 *       strcpy(out_files[count].name, "config.txt");
 *       strcpy(out_files[count].path, "Setting/config.txt");
 *       out_files[count].is_directory = false;
 *       out_files[count].size = 128;
 *       count++;
 *       return count;
 *   }
 */
int App_GetFiles(AppFileInfo *out_files, uint16_t max_count);

/**
 * @brief Read file content
 * @param path File path to read
 * @param out_content Output buffer for file content
 * @param max_len Maximum buffer size
 * @return true on success, false on failure
 *
 * @example
 *   bool App_GetFile(const char *path, char *out_content, uint16_t max_len) {
 *       return SD_ReadFile(path, out_content, max_len) == 0;
 *   }
 */
bool App_GetFile(const char *path, char *out_content, uint16_t max_len);

/**
 * @brief Save content to file
 * @param path File path to write
 * @param content Content to write
 * @return true on success, false on failure
 *
 * @example
 *   bool App_SaveFile(const char *path, const char *content) {
 *       return SD_WriteFile(path, content) == 0;
 *   }
 */
bool App_SaveFile(const char *path, const char *content);

/**
 * @brief Verify file content matches expected content
 * @param path File path to verify
 * @param content Expected content
 * @param out_match Output: true if content matches, false otherwise
 * @return true on success (verification performed), false on failure (couldn't read file)
 *
 * @example
 *   bool App_VerifyFile(const char *path, const char *content, bool *out_match) {
 *       char buffer[512];
 *       if (SD_ReadFile(path, buffer, sizeof(buffer)) != 0) {
 *           return false;  // Couldn't read file
 *       }
 *       *out_match = (strcmp(buffer, content) == 0);
 *       return true;
 *   }
 */
bool App_VerifyFile(const char *path, const char *content, bool *out_match);

/**
 * @brief Get list of all motors with full information
 * @param out_motors Output array of motor info structures
 * @param max_count Maximum number of motors to return
 * @return Number of motors (>= 0) on success, -1 on failure
 *
 * @example
 *   int App_GetMotors(AppMotorInfo *out_motors, uint16_t max_count) {
 *       int idx = 0;
 *       if (idx < max_count) {
 *           out_motors[idx].id = 1;
 *           out_motors[idx].group_id = 1;
 *           out_motors[idx].sub_id = 1;
 *           strcpy(out_motors[idx].type, "Servo");
 *           strcpy(out_motors[idx].status, "Normal");
 *           out_motors[idx].position = 90.0f;
 *           out_motors[idx].velocity = 0.5f;
 *           idx++;
 *       }
 *       return idx;
 *   }
 */
int App_GetMotors(AppMotorInfo *out_motors, uint16_t max_count);

/**
 * @brief Get current state of all motors (for polling)
 * @param out_states Output array of motor state structures
 * @param max_count Maximum number of motors to return
 * @return Number of motors (>= 0) on success, -1 on failure
 *
 * @note This function is called periodically for state updates.
 *       Only id, status, position, and velocity are returned.
 *
 * @example
 *   int App_GetMotorState(AppMotorState *out_states, uint16_t max_count) {
 *       int idx = 0;
 *       if (idx < max_count) {
 *           out_states[idx].id = 1;
 *           strcpy(out_states[idx].status, "Normal");
 *           out_states[idx].position = Motor_GetPosition(1);
 *           out_states[idx].velocity = Motor_GetVelocity(1);
 *           idx++;
 *       }
 *       return idx;
 *   }
 */
int App_GetMotorState(AppMotorState *out_states, uint16_t max_count);

#endif /* INC_DEVICE_HAL_H_ */
