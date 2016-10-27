LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

statics_files := tcpmux.c

LOCAL_C_INCLUDES  += bionic
LOCAL_CFLAGS     += -DNO_ZIP
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE      := tcpmux
LOCAL_SRC_FILES   := $(statics_files)

include $(BUILD_EXECUTABLE)

ifeq ($(WITH_HOST_DALVIK),true)
  include $(CLEAR_VARS)

  LOCAL_CFLAGS     += -DNO_ZIP
  LOCAL_MODULE_TAGS := optional
  LOCAL_MODULE      := tcpmux
  LOCAL_SRC_FILES   := $(statics_files)

  include $(BUILD_HOST_EXECUTABLE)
endif

