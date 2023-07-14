#include"ext.h"            // standard Max include, always required (except in Jitter)
//#include"ext_obex.h"        // required for "new" style objects

#include<CoreMedia/CoreMedia.h>

C74_HIDDEN static t_class const * class = NULL;
C74_HIDDEN static CMTimebaseRef prime = NULL;
C74_HIDDEN static dispatch_queue_t queue = NULL;

typedef struct {
    t_object const super;
    CMTimebaseRef const clock;
    CMTime check;
    CMTime const * const epoch;
    long const count;
    dispatch_source_t const * const tasks;
    t_outlet const * const pulse;
    t_outlet const * const state;
} t_timecode;


C74_HIDDEN CMTimeValue gcd(CMTimeValue const x, CMTimeValue const y) {
    return y == 0 ? x : gcd(y, x % y);
}

C74_HIDDEN CMTime simplify(CMTime const value) {
    CMTimeValue const vs = gcd(value.value, value.timescale);
    return CMTimeMake(value.value / vs, value.timescale / vs);
}

C74_HIDDEN __int128_t __idiv__(CMTime const x, CMTime const y) {
    __int128_t const a = x.value;
    __int128_t const b = x.timescale;
    __int128_t const c = y.value;
    __int128_t const d = y.timescale;
    return ( a * d ) / ( b * c );
}

C74_HIDDEN long const __parse__(CMTime * const target, short const argc, t_atom const * const argv) {
    for ( register short k = 0, K = argc ; k < K ; ++ k )
        target[k] = CMTimeMake(60, (int32_t const)atom_getlong(argv + k));
    return argc;
}

C74_HIDDEN void __fire__(t_timecode const * const this) {
    *(CMTime*const)&this->check = CMTimebaseGetTime(this->clock);
    for ( register long k = 0, K = this->count ; k < K ; ++ k )
        CMTimebaseSetTimerDispatchSourceNextFireTime(this->clock, this->tasks[k], CMTimeMultiply(this->epoch[k], 1 + __idiv__(this->check, this->epoch[k])), 0);
    outlet_bang((t_outlet*const)this->pulse);
}

C74_HIDDEN t_timecode const * const __new__(t_symbol const * const symbol, short const argc, t_atom const * const argv) {
    
    t_timecode const*const this = (t_timecode*const)object_alloc((t_class*const)class);
    
    *(CMTime const**const)&this->epoch = (CMTime const*const)sysmem_newptrclear(argc * sizeof(CMTime));
    
    if ( this ) switch (CMTimebaseCreateWithSourceTimebase(NULL, prime, (CMTimebaseRef*)&this->clock)) {
            
        case 0:
            
            *(t_outlet const**const)&this->pulse = bangout((void*const)this);
            
            *(long*const)&this->count = __parse__((CMTime*const)this->epoch, argc, argv);
            
            *(dispatch_source_t const**const)&this->tasks = (dispatch_source_t const*const)sysmem_newptr((this->count + 1) * sizeof(dispatch_source_t));
            
            for ( register long k = 0, K = this->count ; k < K ; ++ k ) {
                
                CMTime const epoch = this->epoch[K - k - 1];
                
                t_outlet const * const pulse = intout((void*const)this);
                
                dispatch_source_t const source = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
                
                dispatch_source_set_event_handler(source, ^{
                    t_atom_long const count = __idiv__(CMTimebaseGetTime(this->clock), epoch);
                    outlet_int((t_outlet*const)pulse, count);
                    CMTimebaseSetTimerDispatchSourceNextFireTime(this->clock, source, CMTimeMultiply(epoch, 1 + (int32_t const)count), 0);
                });
                
                CMTimebaseAddTimerDispatchSource(this->clock, source);
                
                dispatch_resume(source);
                
                *(dispatch_source_t*const)(this->tasks + k) = source;
                
            }
            
            *(dispatch_source_t**const)(this->tasks + this->count) = NULL;
            
            *(t_outlet const**const)&this->state = listout((void*const)this);
            
            CMTimebaseSetRate(this->clock, 1);
            
            __fire__(this);
            
            break;
            
        default:
            error("clock error");
    }
    
    return this;
}

C74_HIDDEN void __bang__(t_timecode const * const this) {
    CMTime const time = CMTimebaseGetTime(this->clock);
    t_atom vs[2] = {0};
    atom_setlong(vs + 0, (t_atom_long const)time.value);
    atom_setlong(vs + 1, (t_atom_long const)time.timescale);
    outlet_list((t_outlet*const)this->state, gensym("list"), 2, vs);
}

C74_HIDDEN void __remove__(t_timecode const * const this) {
    if ( this->tasks[this->count] ) {
        dispatch_source_cancel(this->tasks[this->count]);
        dispatch_release(this->tasks[this->count]);
        *(dispatch_source_t*const)(this->tasks + this->count) = NULL;
    }
}

C74_HIDDEN void __sync__(t_timecode const * const this, t_symbol const * const symbol, short const argc, t_atom const * const argv) {
    __remove__(this);
    switch ( argc ) {
        case 1: {
            struct sockaddr_in const target = {
                .sin_family = AF_INET,
                .sin_addr = {
                    .s_addr = INADDR_ANY,
                },
                .sin_port = htons(atom_getlong(argv)),
                .sin_len = sizeof(struct sockaddr_in),
                .sin_zero = {0}
            };
            dispatch_source_t const tasks = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, (uintptr_t const)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), 0, queue);
            dispatch_source_set_registration_handler(tasks, ^{
                int const socket = (int const)dispatch_source_get_handle(tasks);
                switch (bind(socket, (struct sockaddr*const)&target, sizeof(struct sockaddr_in))) {
                    case 0:
                        break;
                    default:
                        error("bind error");
                }
            });
            dispatch_source_set_event_handler(tasks, ^{
                int const socket = (int const)dispatch_source_get_handle(tasks);
                CMTime buff[4] = {
                    kCMTimeInvalid,
                    kCMTimeInvalid,
                    CMTimebaseGetTime(this->clock),
                    this->check
                };
                struct sockaddr_in target = {0};
                socklen_t length = sizeof(struct sockaddr_in);
                switch (recvfrom(socket, buff, 2 * sizeof(CMTime), 0, (struct sockaddr*const)&target, &length)) {
                    case 2 * sizeof(CMTime):
                        switch (sendto(socket, buff, 4 * sizeof(CMTime), 0, (struct sockaddr*const)&target, length)) {
                            case 4 * sizeof(CMTime):
                                break;
                            default:
                                error("send error");
                                break;
                        }
                        break;
                    default:
                        error("recv error");
                        break;
                }
            });
            dispatch_source_set_cancel_handler(tasks, ^{
                close((int const)dispatch_source_get_handle(tasks));
            });
            dispatch_resume(tasks);
            *(dispatch_source_t*const)(this->tasks + this->count) = tasks;
            break;
        }
        case 2: {
            __block struct {
                CMTime self;
                CMTime peer;
            } anchor = {
                .self = kCMTimeInvalid,
                .peer = kCMTimeInvalid,
            };
            __block CMTime marked = kCMTimeInvalid;
            __block CMTime length = kCMTimeInvalid;
            struct sockaddr_in const target = {
                .sin_family = AF_INET,
                .sin_addr = {
                    .s_addr = INADDR_ANY,
                },
                .sin_port = htons(atom_getlong(argv)),
                .sin_len = sizeof(struct sockaddr_in),
                .sin_zero = {0}
            };
            inet_pton(AF_INET, atom_getsym(argv + 1)->s_name, (struct in_addr*const)&target.sin_addr.s_addr);
            dispatch_source_t const timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
            dispatch_source_t const tasks = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, (uintptr_t const)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), 0, queue);
            dispatch_source_set_event_handler(timer, ^{
                CMTime const buff[2] = {
                    CMTimebaseGetTime(prime),
                    CMTimebaseGetTime(this->clock)
                };
                switch (sendto((int const)dispatch_source_get_handle(tasks), buff, 2 * sizeof(CMTime), 0, (struct sockaddr*const)&target, sizeof(struct sockaddr_in))) {
                    case 2 * sizeof(CMTime):
                        break;
                    default:
                        error("send error");
                        break;
                }
                CMTimebaseSetTimerDispatchSourceNextFireTime(prime, timer, CMTimeMake((buff[0].value / buff[0].timescale + 1) * buff[0].timescale, buff[0].timescale), 0);
            });
            dispatch_source_set_registration_handler(tasks, ^{
                dispatch_resume(timer);
                CMTimebaseAddTimerDispatchSource(prime, timer);
                CMTimebaseSetTimerDispatchSourceToFireImmediately(prime, timer);
            });
            dispatch_source_set_event_handler(tasks, ^{
                CMTime buff[6] = {
                    kCMTimeInvalid,// self prime clock
                    kCMTimeInvalid,// self this->clock
                    kCMTimeInvalid,// peer clock time
                    kCMTimeInvalid,// peer signature
                    CMTimebaseGetTime(prime),
                    CMTimebaseGetTime(this->clock),
                };
                switch (recv((int const)dispatch_source_get_handle(tasks), buff, 4 * sizeof(CMTime), 0)) {
                    case 4 * sizeof(CMTime):
                        if ( CMTimeCompare(marked, buff[3]) ) {
                            length = kCMTimePositiveInfinity;
                            marked = buff[3];
                        } else {
                            struct {
                                CMTime const time;
                                CMTime const base;
                            } const self = {
                                .time = CMTimeMultiplyByRatio(CMTimeAdd(buff[1], buff[5]), 1, 2),
                                .base = CMTimeMultiplyByRatio(CMTimeAdd(buff[0], buff[4]), 1, 2),
                            }, peer = {
                                .time = buff[2],
                                .base = kCMTimeIndefinite,
                            };
                            switch (CMTimeCompare(CMTimeSubtract(buff[4], buff[0]), length)) {
                                case -1:
                                    length = CMTimeSubtract(buff[4], buff[0]);
                                    anchor.peer = peer.time;
                                    anchor.self = self.base;
                                    break;
                                default:
                                    switch (CMTimeCompare(CMTimeMake(1, 1024), CMTimeAbsoluteValue(CMTimeSubtract(self.time, peer.time)))) {
                                        case -1:
                                            CMTimebaseSetRateAndAnchorTime(this->clock,
                                                                           CMTimeGetSeconds(CMTimeSubtract(peer.time, anchor.peer))/CMTimeGetSeconds(CMTimeSubtract(self.base, anchor.self)), peer.time, self.base);
                                            __fire__(this);
                                            break;
                                    }
                                    break;
                            }
                        }
                        break;
                    default:
                        error("recv error");
                        break;
                }
            });
            dispatch_source_set_cancel_handler(tasks, ^{
                CMTimebaseRemoveTimerDispatchSource(prime, timer);
                dispatch_source_cancel(timer);
                dispatch_release(timer);
            });
            dispatch_resume(tasks);
            *(dispatch_source_t*const)(this->tasks + this->count) = tasks;
        }
            break;
        default:
            error("not allowed");
    }
}

C74_HIDDEN void __rate__(t_timecode const * const this, t_atom_float const value) {
    __remove__(this);
    CMTimebaseSetRate(this->clock, value);
    __fire__(this);
}

C74_HIDDEN void __time__(t_timecode const * const this, t_atom_float const value, t_atom_long const scale) {
    __remove__(this);
    CMTimebaseSetTime(this->clock, CMTimeMakeWithSeconds(value, (int32_t const)MAX(1, scale)));
    __fire__(this);
}

C74_HIDDEN void __del__(t_timecode const * const this) {
    for ( register long k = 0, K = this->count ; k < K ; ++ k ) {
        dispatch_source_cancel(this->tasks[k]);
        CMTimebaseRemoveTimerDispatchSource(this->clock, this->tasks[k]);
        dispatch_release(this->tasks[k]);
    }
    if ( this->tasks[this->count] ) {
        dispatch_source_cancel(this->tasks[this->count]);
        dispatch_release(this->tasks[this->count]);
    }
    sysmem_freeptr((void*const)this->tasks);
    sysmem_freeptr((void*const)this->epoch);
    CFRelease(this->clock);
}

C74_EXPORT void ext_main(void * const _) {
    if ( !queue ) {
        queue = dispatch_queue_create("art.xsgn.timecode", DISPATCH_QUEUE_CONCURRENT);
    }
    if ( !prime ) {
        switch (CMTimebaseCreateWithSourceClock(NULL, CMClockGetHostTimeClock(), &prime)) {
            case noErr:
                CMTimebaseSetRate(prime, 1);
                break;
            default:
                perror("clock error");
                break;
        }
    }
    if ( !class ) {
        class = class_new("timecode", (method const)__new__, (method const)__del__, sizeof(t_timecode), NULL, A_GIMME, 0);
        
        class_addmethod((t_class*const)class, (method const)__bang__, "bang", 0);
        class_addmethod((t_class*const)class, (method const)__sync__, "sync", A_GIMME, 0);
        class_addmethod((t_class*const)class, (method const)__rate__, "rate", A_FLOAT, 0);
        class_addmethod((t_class*const)class, (method const)__time__, "time", A_FLOAT, A_DEFLONG, 0);
        
        class_register(CLASS_BOX, (t_class*const)class);
    }
}


//typedef struct {
//    CMTimebaseRef master;
//    dispatch_source_t source;
//    t_outlet * outlet;
//    CMTime period;
//} t_ticker;
//
//typedef struct {
//    CMTimebaseRef master;
//    dispatch_source_t source;
//    t_outlet * outlet;
//    struct sockaddr_in target;
//} t_linker;
//
//C74_HIDDEN void __client__regist__(t_linker * const this) {
//    CMTimebaseAddTimerDispatchSource(this->master, this->source);
//}
//
//C74_HIDDEN void __client__handle__(t_linker * const this) {
//
//}
//
//C74_HIDDEN void __client__cancel__(t_linker * const this) {
//    int const handle = (int const)dispatch_source_get_handle(this->source);
//    CMTimebaseRemoveTimerDispatchSource(this->master, this->source);
//    close(handle);
//}



//
//typedef struct {
//    t_object const super;
//    CMTimebaseRef timebase;
//    dispatch_source_t network;
//    long numtick;
//    t_ticker * tickers;
//    t_outlet * listout;
//    t_outlet * bangout;
//    CMTime anchor;
//} t_timecode;
//
//C74_HIDDEN CMTimeValue gcd(CMTimeValue x, CMTimeValue y) {
//    return y ? gcd(y, x % y) : x;
//}
//
//C74_HIDDEN CMTime __simplify__(CMTime const time) {
//    CMTimeValue const value = gcd(time.value, time.timescale);
//    return CMTimeMake(time.value / value, time.timescale / value);
//}
//
//C74_HIDDEN CMTimeScale __idiv__(CMTime const x, CMTime const y) {
//    register __int128_t const a = x.value;
//    register __int128_t const b = x.timescale;
//    register __int128_t const c = y.value;
//    register __int128_t const d = y.timescale;
//    return ( a * d ) / ( b * c );
//}
//
//C74_HIDDEN CMTime __bpm2dur__(Float64 scale) {
//    int64_t value = 60;
//    while ( FLT_MIN < fabs(scale - trunc(scale)) && log2(scale) < 20 ) {
//        value *= 2;
//        scale *= 2;
//    }
//    return __simplify__(CMTimeMake(value, scale));
//}
//
//C74_HIDDEN void __tickers__(t_ticker * const this) {
//    CMTimeScale const count = __idiv__(CMTimebaseGetTime(this->master), this->period);
//    outlet_int(this->outlet, count);
//    CMTimebaseSetTimerDispatchSourceNextFireTime(this->master, this->source, CMTimeMultiply(this->period, 1 + count), 0);
//}
//
//C74_HIDDEN void __fire__(t_timecode * const this) {
//    CMTime const now = CMTimebaseGetTime(this->timebase);
//    for ( register long k = 0, K = this->numtick ; k < K ; ++ k )
//        CMTimebaseSetTimerDispatchSourceNextFireTime(this->timebase, this->tickers[k].source, CMTimeMultiply(this->tickers[k].period, 1 + __idiv__(now, this->tickers[k].period)), 0);
//    outlet_bang(this->bangout);
//}
//
//C74_HIDDEN void*__new__(t_symbol const * const symbol, short const argc, t_atom const*const argv) {
//    t_timecode * const this = (t_timecode*const)object_alloc((t_class*const)__class__);
//    if ( this ) {
//
//        if ( noErr != CMTimebaseCreateWithSourceClock(kCFAllocatorDefault, __clock__, &this->timebase) )
//            return NULL;
//
//        this->network = NULL;
//        this->tickers = NULL;
//
//        this->bangout = bangout(this);
//
//        this->numtick = argc;
//        if ( 0 < this->numtick ) {
//            double * intervals = (double*)alloca(argc * sizeof(double));
//            switch (atom_getdouble_array(argc, (t_atom*)argv, argc, intervals)) {
//                case 0:
//                    this->tickers = (t_ticker*const)sysmem_newptr(argc * sizeof(t_ticker));
//                    for ( register int k = 0, K = argc ; k < K ; ++ k ) {
//                        this->tickers[k].outlet = intout(this);
//                        this->tickers[k].master = this->timebase;
//                        this->tickers[k].period = __bpm2dur__(intervals[K - k - 1]);
//                        this->tickers[k].source = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, __queue__);
//                        dispatch_set_context(this->tickers[k].source, this->tickers + k);
//                        dispatch_source_set_event_handler_f(this->tickers[k].source, (void(*const)(void*const))__tickers__);
//                        dispatch_resume(this->tickers[k].source);
//                        CMTimebaseAddTimerDispatchSource(this->timebase, this->tickers[k].source);
//                    }
//                    __fire__(this);
//                    break;
//                default:
//                    error("invalid arguments");
//            }
//        }
//
//        this->listout = listout(this);
//
//        CMTimebaseSetRate(this->timebase, 1);
//
//    }
//    return this;
//
//}
//C74_HIDDEN void __cancel__(t_timecode * const this) {
//    if ( this->network ) {
//        dispatch_source_cancel(this->network);
//        dispatch_release(this->network);
//        this->network = NULL;
//    }
//}
//C74_HIDDEN void __del__(t_timecode * const this) {
//    __cancel__(this);
//    if ( this->tickers ) {
//        for ( register long k = 0, K = this->numtick ; k < K ; ++ k ) {
//            CMTimebaseRemoveTimerDispatchSource(this->timebase, this->tickers[k].source);
//            dispatch_source_cancel(this->tickers[k].source);
//            dispatch_release(this->tickers[k].source);
//        }
//        sysmem_freeptr(this->tickers);
//    }
//    CFRelease(this->timebase);
//}
//C74_HIDDEN void __bang__(t_timecode const * const this) {
//    CMTime const now = CMTimebaseGetTime(this->timebase);
//    t_atom time[2] = {0};
//    atom_setlong(time+0, now.value);
//    atom_setlong(time+1, now.timescale);
//    outlet_list(this->listout, gensym("list"), 2, time);
//}
//C74_HIDDEN void __time__(t_timecode * const this, long const value, long const scale) {
//    __cancel__(this);
//    CMTimebaseSetTime(this->timebase, CMTimeMake(value, (int32_t)scale));
//    __fire__(this);
//}
//C74_HIDDEN void __rate__(t_timecode * const this, double const rate) {
//    __cancel__(this);
//
//    CMTimebaseSetRate(this->timebase, rate);
//    __fire__(this);
//}
//
//typedef enum  {
//    SERVER,
//    CLIENT
//} t_mode;
//typedef struct {
//    t_timecode const * const ref;
//    t_mode mode;
//} t_sample;
//typedef struct {
//    t_timecode const * const timecode;
//} t_client;
//typedef struct {
//    t_timecode const * const timecode;
//} t_server;
//
//C74_HIDDEN void __sync__(t_timecode * const this, t_symbol const * const symbol, short const argc, t_atom const * const argv) {
//    __cancel__(this);
//    int handle = 0;
//    switch ( argc ) {
//        case 1:
//            if ( 0 < ( handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) ) ) {
//                struct sockaddr_in const target = {
//                    .sin_family = AF_INET,
//                    .sin_addr = {
//                        .s_addr = INADDR_ANY
//                    },
//                    .sin_port = htons(atom_getlong(argv + 0)),
//                    .sin_len = sizeof(struct sockaddr_in),
//                    .sin_zero = {0}
//                };
//                dispatch_source_t const source = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, (uintptr_t const)handle, 0, __queue__);
//                dispatch_source_set_registration_handler(source, ^{
//                    int const socket = (int const)dispatch_source_get_handle(source);
//                    switch (bind(socket, (struct sockaddr*const)&target, sizeof(struct sockaddr_in))) {
//                        case 0:
//                            post("bind");
//                            break;
//                        default:
//                            error("bind");
//                            break;
//                    }
//                    post("%p", dispatch_get_context(source));
//                });
//                dispatch_source_set_event_handler(source, ^{
//                    int const socket = (int const)dispatch_source_get_handle(source);
//                    CMTime buffer[3] = {
//                        kCMTimeInvalid,
//                        kCMTimeInvalid,
//                        CMTimebaseGetTime(this->timebase),
//                    };
//                    struct sockaddr_in target = {0};
//                    socklen_t length = sizeof(target);
//                    switch (recvfrom(handle, buffer, 2 * sizeof(CMTime), 0, (struct sockaddr*)&target, &length)) {
//                        case 2 * sizeof(CMTime):
//                            switch (sendto(handle, buffer, 3 * sizeof(CMTime), 0, (struct sockaddr*)&target, length)) {
//                                case 3 * sizeof(CMTime):
//                                    post("handle success");
//                                    break;
//                                default:
//                                    error("send error");
//                            }
//                            break;
//                        default:
//                            error("recv error");
//                    }
//                });
//                dispatch_source_set_cancel_handler(source, ^{
//                    close((int const)dispatch_source_get_handle(source));
//                    post("close");
//                });
//                dispatch_resume(source);
//                this->network = source;
//            }
//            break;
//        case 3: {
//            dispatch_source_t const source = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, (uintptr_t const)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), 0, __queue__);
//            dispatch_resume(source);
//        }
//            if ( 0 < ( handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) ) ) {
//                struct sockaddr_in const target = {
//                    .sin_family = AF_INET,
//                    .sin_addr = {
//                        .s_addr = INADDR_ANY
//                    },
//                    .sin_port = htons(atom_getlong(argv + 0)),
//                    .sin_len = sizeof(struct sockaddr_in),
//                    .sin_zero = {0}
//                };
//                inet_aton(atom_getsym(argv + 1)->s_name, (struct in_addr*const)&target.sin_addr.s_addr);
//                dispatch_source_t const source = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, __queue__);
//                dispatch_source_set_registration_handler(source, ^{
//
//                });
//                dispatch_source_set_event_handler(source, ^{
//                    int const socket = handle;
//                    CMTime buff[4] = {
//                        CMTimebaseGetTime(this->timebase),
//                        CMClockGetTime(__clock__),
//                        kCMTimeInvalid,
//                        kCMTimeInvalid,
//                    };
//                    socklen_t length = sizeof(struct sockaddr_in);
//                    sendto(socket, buff, 2 * sizeof(CMTime), 0, (struct sockaddr*const)&target, length);
//                    recvfrom(socket, buff, 3 * sizeof(CMTime), 0, (struct sockaddr*const)&target, &length);
//                    buff[3] = CMClockGetTime(__clock__);
//                    CMTime const midd = CMTimeMultiplyByRatio(CMTimeAdd(buff[3], buff[1]), 1, 2);
////                    post("%ld, %u", buff[2].value, buff[2].timescale);
////                    CMTimebaseSetRateAndAnchorTime(this->timebase, 1, buff[2], midd);
//                    CMTimebaseSetTime(this->timebase, buff[2]);
//                    CMTimebaseSetAnchorTime(this->timebase, buff[2], midd);
//                    __fire__(this);
//                });
//                dispatch_source_set_cancel_handler(source, ^{
//                    close(handle);
//                    post("client close");
//                });
//                dispatch_source_set_timer(source, DISPATCH_TIME_NOW, atom_getlong(argv + 2) * NSEC_PER_SEC, 0);
//                dispatch_resume(source);
//                this->network = source;
//            }
//            break;
//    }
//}
//C74_EXPORT void ext_main(void * const _) {
//
//    if ( !__clock__ )
//        __clock__ = CMClockGetHostTimeClock();
//
//    if ( !__queue__ )
//        __queue__ = dispatch_queue_create("art.xsgn.timecode", DISPATCH_QUEUE_CONCURRENT);
//
//    if ( !__class__ ) {
//        t_class * const class = class_new("timecode", (method const)__new__, (method const)__del__, sizeof(t_timecode), NULL, A_GIMME, 0);
//
//        class_addmethod(class, (method const)__bang__, "bang", 0);
//        class_addmethod(class, (method const)__time__, "time", A_LONG, A_LONG, 0);
//        class_addmethod(class, (method const)__rate__, "rate", A_FLOAT, 0);
//        class_addmethod(class, (method const)__sync__, "sync", A_GIMME, 0);
//
//        class_register(CLASS_BOX, class);
//        __class__ = class;
//    }
//}
