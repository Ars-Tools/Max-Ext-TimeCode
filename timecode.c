#include"ext.h"            // standard Max include, always required (except in Jitter)
//#include"ext_obex.h"        // required for "new" style objects

#include<CoreMedia/CoreMedia.h>

C74_HIDDEN static t_class const * __class__ = NULL;
C74_HIDDEN static CMClockRef __clock__ = NULL;
C74_HIDDEN static dispatch_queue_t __queue__ = NULL;

typedef struct {
    CMTimebaseRef master;
    dispatch_source_t source;
    t_outlet * outlet;
    CMTime period;
} t_ticker;

typedef struct {
    CMTimebaseRef master;
    dispatch_source_t source;
    t_outlet * outlet;
    struct sockaddr_in target;
} t_linker;

C74_HIDDEN void __client__regist__(t_linker * const this) {
    CMTimebaseAddTimerDispatchSource(this->master, this->source);
}

C74_HIDDEN void __client__handle__(t_linker * const this) {
    
}

C74_HIDDEN void __client__cancel__(t_linker * const this) {
    int const handle = (int const)dispatch_source_get_handle(this->source);
    CMTimebaseRemoveTimerDispatchSource(this->master, this->source);
    close(handle);
}

typedef struct {
    t_object const super;
    CMTimebaseRef timebase;
    dispatch_source_t network;
    long numtick;
    t_ticker * tickers;
    t_outlet * listout;
    t_outlet * bangout;
    CMTime anchor;
} t_timecode;

C74_HIDDEN CMTimeValue gcd(CMTimeValue x, CMTimeValue y) {
    return y ? gcd(y, x % y) : x;
}

C74_HIDDEN CMTime __simplify__(CMTime const time) {
    CMTimeValue const value = gcd(time.value, time.timescale);
    return CMTimeMake(time.value / value, time.timescale / value);
}

C74_HIDDEN CMTimeScale __idiv__(CMTime const x, CMTime const y) {
    register __int128_t const a = x.value;
    register __int128_t const b = x.timescale;
    register __int128_t const c = y.value;
    register __int128_t const d = y.timescale;
    return ( a * d ) / ( b * c );
}

C74_HIDDEN CMTime __bpm2dur__(Float64 scale) {
    int64_t value = 60;
    while ( FLT_MIN < fabs(scale - trunc(scale)) && log2(scale) < 20 ) {
        value *= 2;
        scale *= 2;
    }
    return __simplify__(CMTimeMake(value, scale));
}

C74_HIDDEN void __tickers__(t_ticker * const this) {
    CMTimeScale const count = __idiv__(CMTimebaseGetTime(this->master), this->period);
    outlet_int(this->outlet, count);
    CMTimebaseSetTimerDispatchSourceNextFireTime(this->master, this->source, CMTimeMultiply(this->period, 1 + count), 0);
}

C74_HIDDEN void __fire__(t_timecode * const this) {
    CMTime const now = CMTimebaseGetTime(this->timebase);
    for ( register long k = 0, K = this->numtick ; k < K ; ++ k )
        CMTimebaseSetTimerDispatchSourceNextFireTime(this->timebase, this->tickers[k].source, CMTimeMultiply(this->tickers[k].period, 1 + __idiv__(now, this->tickers[k].period)), 0);
    outlet_bang(this->bangout);
}

C74_HIDDEN void*__new__(t_symbol const * const symbol, short const argc, t_atom const*const argv) {
    t_timecode * const this = (t_timecode*const)object_alloc((t_class*const)__class__);
    if ( this ) {
        
        if ( noErr != CMTimebaseCreateWithSourceClock(kCFAllocatorDefault, CMClockGetHostTimeClock(), &this->timebase) )
            return NULL;
        
        this->network = NULL;
        this->tickers = NULL;
        
        this->bangout = bangout(this);
        
        this->numtick = argc;
        if ( 0 < this->numtick ) {
            double * intervals = (double*)alloca(argc * sizeof(double));
            switch (atom_getdouble_array(argc, (t_atom*)argv, argc, intervals)) {
                case 0:
                    this->tickers = (t_ticker*const)sysmem_newptr(argc * sizeof(t_ticker));
                    for ( register int k = 0, K = argc ; k < K ; ++ k ) {
                        this->tickers[k].outlet = intout(this);
                        this->tickers[k].master = this->timebase;
                        this->tickers[k].period = __bpm2dur__(intervals[K - k - 1]);
                        this->tickers[k].source = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, __queue__);
                        dispatch_set_context(this->tickers[k].source, this->tickers + k);
                        dispatch_source_set_event_handler_f(this->tickers[k].source, (void(*const)(void*const))__tickers__);
                        dispatch_resume(this->tickers[k].source);
                        CMTimebaseAddTimerDispatchSource(this->timebase, this->tickers[k].source);
                    }
                    __fire__(this);
                    break;
                default:
                    error("invalid arguments");
            }
        }
        
        this->listout = listout(this);
        
        CMTimebaseSetRate(this->timebase, 1);
        
    }
    return this;
    
}
C74_HIDDEN void __cancel__(t_timecode * const this) {
    if ( this->network ) {
        dispatch_source_cancel(this->network);
        dispatch_release(this->network);
        this->network = NULL;
    }
}
C74_HIDDEN void __del__(t_timecode * const this) {
    __cancel__(this);
    if ( this->tickers ) {
        for ( register long k = 0, K = this->numtick ; k < K ; ++ k ) {
            CMTimebaseRemoveTimerDispatchSource(this->timebase, this->tickers[k].source);
            dispatch_source_cancel(this->tickers[k].source);
            dispatch_release(this->tickers[k].source);
        }
        sysmem_freeptr(this->tickers);
    }
    CFRelease(this->timebase);
}
C74_HIDDEN void __bang__(t_timecode const * const this) {
    CMTime const now = CMTimebaseGetTime(this->timebase);
    t_atom time[2] = {0};
    atom_setlong(time+0, now.value);
    atom_setlong(time+1, now.timescale);
    outlet_list(this->listout, gensym("list"), 2, time);
}
C74_HIDDEN void __time__(t_timecode * const this, long const value, long const scale) {
    __cancel__(this);
    CMTimebaseSetTime(this->timebase, CMTimeMake(value, (int32_t)scale));
    __fire__(this);
}
C74_HIDDEN void __rate__(t_timecode * const this, double const rate) {
    __cancel__(this);
    CMTimebaseSetRate(this->timebase, rate);
    __fire__(this);
}
C74_HIDDEN void __sync__(t_timecode * const this, t_symbol const * const symbol, short const argc, t_atom const * const argv) {
    __cancel__(this);
    int handle = 0;
    switch ( argc ) {
        case 1:
            if ( 0 < ( handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) ) ) {
                struct sockaddr_in const target = {
                    .sin_family = AF_INET,
                    .sin_addr = {
                        .s_addr = INADDR_ANY
                    },
                    .sin_port = htons(atom_getlong(argv + 0)),
                    .sin_len = sizeof(struct sockaddr_in),
                    .sin_zero = {0}
                };
                dispatch_source_t const source = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, (uintptr_t const)handle, 0, __queue__);
                dispatch_source_set_registration_handler(source, ^{
                    int const socket = (int const)dispatch_source_get_handle(source);
                    switch (bind(socket, (struct sockaddr*const)&target, sizeof(struct sockaddr_in))) {
                        case 0:
                            post("bind");
                            break;
                        default:
                            error("bind");
                            break;
                    }
                    post("%p", dispatch_get_context(source));
                });
                dispatch_source_set_event_handler(source, ^{
                    int const socket = (int const)dispatch_source_get_handle(source);
                    CMTime buffer[3] = {
                        kCMTimeInvalid,
                        kCMTimeInvalid,
                        CMTimebaseGetTime(this->timebase),
                    };
                    struct sockaddr_in target = {0};
                    socklen_t length = sizeof(target);
                    switch (recvfrom(handle, buffer, 2 * sizeof(CMTime), 0, (struct sockaddr*)&target, &length)) {
                        case 2 * sizeof(CMTime):
                            switch (sendto(handle, buffer, 3 * sizeof(CMTime), 0, (struct sockaddr*)&target, length)) {
                                case 3 * sizeof(CMTime):
                                    post("handle success");
                                    break;
                                default:
                                    error("send error");
                            }
                            break;
                        default:
                            error("recv error");
                    }
                });
                dispatch_source_set_cancel_handler(source, ^{
                    close((int const)dispatch_source_get_handle(source));
                    post("close");
                });
                dispatch_resume(source);
                this->network = source;
            }
            break;
        case 3:
            if ( 0 < ( handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) ) ) {
                struct sockaddr_in const target = {
                    .sin_family = AF_INET,
                    .sin_addr = {
                        .s_addr = INADDR_ANY
                    },
                    .sin_port = htons(atom_getlong(argv + 0)),
                    .sin_len = sizeof(struct sockaddr_in),
                    .sin_zero = {0}
                };
                inet_aton(atom_getsym(argv + 1)->s_name, (struct in_addr*const)&target.sin_addr.s_addr);
                dispatch_source_t const source = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, __queue__);
                dispatch_source_set_registration_handler(source, ^{

                });
                dispatch_source_set_event_handler(source, ^{
                    int const socket = handle;
                    CMTime buff[4] = {
                        CMTimebaseGetTime(this->timebase),
                        CMClockGetTime(__clock__),
                        kCMTimeInvalid,
                        kCMTimeInvalid,
                    };
                    socklen_t length = sizeof(struct sockaddr_in);
                    sendto(socket, buff, 2 * sizeof(CMTime), 0, (struct sockaddr*const)&target, length);
                    recvfrom(socket, buff, 3 * sizeof(CMTime), 0, (struct sockaddr*const)&target, &length);
                    buff[3] = CMClockGetTime(__clock__);
                    CMTime const midd = CMTimeMultiplyByRatio(CMTimeAdd(buff[3], buff[1]), 1, 2);
//                    post("%ld, %u", buff[2].value, buff[2].timescale);
//                    CMTimebaseSetRateAndAnchorTime(this->timebase, 1, buff[2], midd);
                    CMTimebaseSetTime(this->timebase, buff[2]);
                    CMTimebaseSetAnchorTime(this->timebase, buff[2], midd);
                });
                dispatch_source_set_cancel_handler(source, ^{
                    close(handle);
                    post("client close");
                });
                dispatch_source_set_timer(source, DISPATCH_TIME_NOW, atom_getlong(argv + 2) * NSEC_PER_SEC, 0);
                dispatch_resume(source);
                this->network = source;
            }
            break;
    }
}
t_max_err __setter__(t_timecode * const this, void * attr, long ac, t_atom * argv) {
    error("setter");
    return MAX_ERR_NONE;
}
t_max_err __getter__(t_timecode * const this, void * attr, long * ac, t_atom ** argv) {
    error("getter");
    return MAX_ERR_NONE;
}
C74_EXPORT void ext_main(void * const _) {
    
    if ( !__clock__ )
        __clock__ = CMClockGetHostTimeClock();
    
    if ( !__queue__ )
        __queue__ = dispatch_queue_create("art.xsgn.timecode", DISPATCH_QUEUE_CONCURRENT);
    
    if ( !__class__ ) {
        t_class * const class = class_new("timecode", (method const)__new__, (method const)__del__, sizeof(t_timecode), NULL, A_GIMME, 0);
        
        class_addmethod(class, (method const)__bang__, "bang", 0);
        class_addmethod(class, (method const)__time__, "time", A_LONG, A_LONG, 0);
        class_addmethod(class, (method const)__rate__, "rate", A_FLOAT, 0);
        class_addmethod(class, (method const)__sync__, "sync", A_GIMME, 0);
        
        class_register(CLASS_BOX, class);
        __class__ = class;
    }
}
