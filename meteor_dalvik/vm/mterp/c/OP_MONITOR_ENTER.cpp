HANDLE_OPCODE(OP_MONITOR_ENTER /*vAA*/)
    {
        Object* obj;

        vsrc1 = INST_AA(inst);
        ILOGV("|monitor-enter v%d %s(0x%08x)",
            vsrc1, kSpacing+6, GET_REGISTER(vsrc1));
        obj = (Object*)GET_REGISTER(vsrc1);
        if (!checkForNullExportPC(obj, fp, pc))
            GOTO_exceptionThrown();
        ILOGV("+ locking %p %s", obj, obj->clazz->descriptor);
        EXPORT_PC();    /* need for precise GC */
#ifdef CHECK_STACK_INTEGRITY_DO
        CHECK_STACK_INTEGRITY_DO(
            dvmLockObject(self, obj)
        );
#else
        dvmLockObject(self, obj);
#endif
    }
    FINISH(1);
OP_END
