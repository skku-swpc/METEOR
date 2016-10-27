LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

statics_files := cometmanager.c

LOCAL_C_INCLUDES  += bionic
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE      := cometmanager
LOCAL_SRC_FILES   := $(statics_files)

include $(BUILD_EXECUTABLE)
