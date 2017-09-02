#ifdef DEBUG
    #define dtrace(args ...) fprintf(stderr, args)
#else
    #define dtrace(args ...)
#endif
