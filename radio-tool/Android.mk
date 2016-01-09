LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := radio-tool.c
LOCAL_MODULE_PATH := $(TARGET_OUT)/xbin/
LOCAL_MODULE := radio-tool
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
