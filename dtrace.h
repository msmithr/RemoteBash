#ifdef DEBUG
    #define DTRACE(args ...) fprintf(stderr, args)
#else
    #define DTRACE(args ...)
#endif
