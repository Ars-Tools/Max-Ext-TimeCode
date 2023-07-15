#include"ext.h"            // standard Max include, always required (except in Jitter)
//#include"ext_obex.h"        // required for "new" style objects

#include<CoreMedia/CoreMedia.h>

C74_HIDDEN static t_class const * class = NULL;
C74_HIDDEN static CMTimebaseRef prime = NULL;
C74_HIDDEN static dispatch_queue_t queue = NULL;

typedef struct {
    t_object const super;
    CMTimebaseRef const clock;
    long const count;
    dispatch_source_t const * const tasks;
    t_outlet const * const pulse;
    t_outlet const * const state;
    CMTime const * const epoch;
    CMTime const limit;
    CMTime check;
} t_timecode;

C74_HIDDEN CMTimeValue gcd(CMTimeValue const x, CMTimeValue const y) {
    return y == 0 ? x : gcd(y, x % y);
}

C74_HIDDEN __int128_t __idiv__(CMTime const x, CMTime const y) {
    __int128_t const a = x.value;
    __int128_t const b = x.timescale;
    __int128_t const c = y.value;
    __int128_t const d = y.timescale;
    return ( a * d ) / ( b * c );
}

C74_HIDDEN CMTime __simplify__(CMTime const target) {
    CMTimeValue const vs = gcd(target.value, target.timescale);
    return CMTimeMake(target.value / vs, target.timescale / vs);
}

C74_HIDDEN CMTime const __round__(double value, double scale) {
    double integer = 0;
    while ((FLT_MIN < fabs(modf(scale, &integer)) && log2(scale) < 20) || (FLT_MIN < fabs(modf(value, &integer)) && log2(value) < 20)) {
        value *= 2;
        scale *= 2;
    }
    return CMTimeMake(value, scale);
}

C74_HIDDEN short const __parse__(CMTime * const target, short const argc, t_atom const * const argv) {
    short cursor = 0;
    for ( register short k = 0, K = argc ; k < K ; ++ k )
        switch (atom_gettype(argv+k)) {
            case A_LONG:
                target[cursor++] = __simplify__(CMTimeMake(60, (int32_t const)atom_getlong(argv+k)));
                break;
            case A_FLOAT:
                target[cursor++] = __simplify__(__round__(60, atom_getfloat(argv+k)));
                break;
            case A_SYM: {
                double scale = 1;
                double value = 0;
                switch ( sscanf(atom_getsym(argv+k)->s_name, "%lf/%lf", &value, &scale) ) {
                    case 2:
                        target[cursor++] = __simplify__(__round__(60 * scale, value));
                        break;
                    default:
                        error("invalid form %s", atom_getsym(argv+k)->s_name);
                        break;
                }
                break;
            }
            default:
                error("invalid argument");
                break;
        }
    return cursor;
}

C74_HIDDEN void __fire__(t_timecode const * const this) {
    *(CMTime*const)&this->check = CMTimeAdd(CMTimeMake(1, 1024), CMTimebaseGetTime(this->clock));
    for ( register long k = 0, K = this->count ; k < K ; ++ k )
        CMTimebaseSetTimerDispatchSourceNextFireTime(this->clock, this->tasks[k], CMTimeMultiply(this->epoch[k], 1 + __idiv__(this->check, this->epoch[k])), 0);
    outlet_bang((t_outlet*const)this->pulse);
}

C74_HIDDEN t_timecode const * const __new__(t_symbol const * const symbol, short const argc, t_atom const * const argv) {
    
    t_timecode const*const this = (t_timecode*const)object_alloc((t_class*const)class);
    
    if ( this ) switch (CMTimebaseCreateWithSourceTimebase(NULL, prime, (CMTimebaseRef*)&this->clock)) {
            
        case 0:
            
            *(CMTime const**const)&this->epoch = (CMTime const*const)sysmem_newptrclear(argc * sizeof(CMTime));
            
            *(CMTime*const)&this->limit = CMTimeMake(1, 1024);
            
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
            break;
        }
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
    CMTimebaseSetTime(this->clock, __simplify__(__round__(value, MAX(1, scale))));
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

C74_HIDDEN void __note__(t_timecode const * const this, void const * const b, long const m, long const a, char * const s) {
    if ( m == ASSIST_INLET )
        sprintf_tr(s, "\
bang: output the elapsed time\r\n\
sync [PORT]: export the clock via UDP (PORT)\r\n\
sync [PORT] [HOST]: import the clock from (HOST):(PORT)");
    else if ( a == 0 )
        sprintf_tr(s, "output elapsed time when bang message is received");
    else if ( a == this->count + 1 )
        sprintf_tr(s, "output bang message when the clock is resynchronized");
    else
        sprintf_tr(s, "output elapsed count every %lld/%d (≒%.3lf) second", this->epoch[a-1].value, this->epoch[a-1].timescale, CMTimeGetSeconds(this->epoch[a-1]));
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
        class_addmethod((t_class*const)class, (method const)__note__, "assist", A_CANT, 0);
        class_register(CLASS_BOX, (t_class*const)class);
    }
}
