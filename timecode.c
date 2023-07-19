#include"ext.h"            // standard Max include, always required (except in Jitter)
//#include"ext_obex.h"        // required for "new" style objects

//#include <stdatomic.h>
#include<CoreMedia/CoreMedia.h>

C74_HIDDEN static t_class const * class = NULL;
C74_HIDDEN static CMTimebaseRef prime = NULL;
C74_HIDDEN static dispatch_queue_t queue = NULL;

typedef struct {
    __int128_t const p;
    __int128_t const q;
} rational256_t;

C74_HIDDEN __uint128_t const gcd(__uint128_t const x, __uint128_t const y) {
    return y ? gcd(y, x % y) : x;
}

#define RationalMake(x, y) ((rational256_t const) { .p = x, .q = y })

#define RationalIsNormal(x) (!!x.q)

#define RationalToReal(x) ((long double)x.p / (long double)x.q)

#define RationalIntegral(x) (x.p / x.q)

#define RationalFraction(x) ((rational256_t const) { .p = x.p % x.q, .q = x.q })

C74_HIDDEN rational256_t const RationalNormalize(rational256_t const number) {
    if ( number.q )
        return number.q < 0 ?
            (rational256_t const) {
                .p = -number.p,
                .q = -number.q,
            } :
            (rational256_t const) {
                .p =  number.p,
                .q =  number.q,
            };
    else if ( 0 < number.p )
        return (rational256_t const) {
            .p = +1,
            .q =  0,
        };
    else if ( 0 > number.p )
        return (rational256_t const) {
            .p = -1,
            .q =  0,
        };
    else
        return (rational256_t const) {
            .p =  0,
            .q =  0,
        };
            
}

C74_HIDDEN rational256_t const RationalSimplify(rational256_t const number) {
    rational256_t const normal = RationalNormalize(number);
    if ( normal.q ) {
        assert(0 < normal.q);
        __int128_t const divisor = gcd(ABS(normal.p), normal.q);
        return (rational256_t const) {
            .p = normal.p / divisor,
            .q = normal.q / divisor
        };
    }
    else return normal;
}

C74_HIDDEN rational256_t const RationalNeg(rational256_t const r) {
    return (rational256_t const) {
        .p = -r.p,
        .q =  r.q
    };
}

C74_HIDDEN rational256_t const RationalAdd(rational256_t const x, rational256_t const y) {
    return (rational256_t const) {
        .p = ( x.p * y.q ) + ( y.p * x.q ),
        .q = x.q * y.q
    };
}

C74_HIDDEN rational256_t const RationalSub(rational256_t const x, rational256_t const y) {
    return (rational256_t const) {
        .p = ( x.p * y.q ) - ( y.p * x.q ),
        .q = x.q * y.q
    };
}

C74_HIDDEN rational256_t const RationalMul(rational256_t const x, rational256_t const y) {
    return (rational256_t const) {
        .p = x.p * y.p,
        .q = x.q * y.q
    };
}

C74_HIDDEN rational256_t const RationalDiv(rational256_t const x, rational256_t const y) {
    return (rational256_t const) {
        .p = x.p * y.q,
        .q = x.q * y.p
    };
}

C74_HIDDEN rational256_t const RationalMod(rational256_t const x, rational256_t const y) {
    return (rational256_t const) {
        .p = ( x.p * y.q ) % ( y.p * x.q ),
        .q = ( x.q * y.q )
    };
}

C74_HIDDEN rational256_t const RationalMakeWithReal(long double const real) {
    __int128_t const N = 1ULL << DBL_MANT_DIG;
    long double rest, frac = modfl(real, &rest);
    if ( !frac ) {
        return (rational256_t const) {
            .p = rest,
            .q = 1
        };
    } else if ( frac < 0 ) {
        --rest;
        ++frac;
    }
    assert(0 < frac);
    assert(frac < 1);
    __int128_t a = 0, b = 1;
    __int128_t c = 1, d = 0;
    while ( ( b < N ) && ( d < N ) ) {
        long double const e = fmal(frac, (b+d), -(a+c));
        if ( fabsl( e ) < FLT_EPSILON ) {
            break;
        } else if ( e > 0 ) {
            a += c;
            b += d;
        } else if ( e < 0 ) {
            c += a;
            d += b;
        }
    }
    if ( N >= b + d )
        return (rational256_t const) {
            .p = ( a + c ) + rest * ( b + d ),
            .q = ( b + d ),
        };
    else if ( b < d )
        return (rational256_t const) {
            .p = c + rest * d,
            .q = d
        };
    else if ( b > d )
        return (rational256_t const) {
            .p = a + rest * b,
            .q = b
        };
    else
        return (rational256_t const) {
            .p = ( a + c ) / 2 + rest * ( b + d ) / 2,
            .q = ( b + d ) / 2
        };
}

#define RationalMakeWithCMTime(x) ((rational256_t const){.p = x.value, .q = (CMTimeScale)x.timescale })

C74_HIDDEN CMTime const CMTimeMakeWithRationalNumber(rational256_t const number) {
    rational256_t const value = RationalSimplify(number);
    assert(0 < value.q);
    __int128_t const scale = MAX(1, value.q / ( 1ul << 31 ));
    return CMTimeMake(value.p / scale, value.q / scale);
}

C74_HIDDEN CMTime const CMTimeSimplify(CMTime const time) {
    CMTimeScale const divisor =
        time.timescale < 0 ? -gcd(ABS(time.value), ABS(time.timescale)):
        time.timescale > 0 ?  gcd(ABS(time.value), ABS(time.timescale)):
        1;
    return CMTimeMake(time.value / divisor, time.timescale / divisor);
}

C74_HIDDEN __int128_t const CMTimeDiv(CMTime const x, CMTime const y) {
    __int128_t const a = x.value;
    __int128_t const b = x.timescale;
    __int128_t const c = y.value;
    __int128_t const d = y.timescale;
    return ( a * d ) / ( b * c );
}

C74_HIDDEN bool const CMTimeMakeWithAtomAsBPM(t_atom const * const source, CMTime * const result) {
    if ( result )
        switch ( atom_gettype(source) ) {
            case A_LONG:
                *result = CMTimeMake(60, (CMTimeScale const)ABS(atom_getlong(source)));
                return true;
            case A_FLOAT:
                *result = CMTimeMakeWithRationalNumber(RationalDiv(RationalMake(60, 1), RationalMakeWithReal(fabs(atom_getfloat(source)))));
                return true;
            case A_SYM: {
                char const * const string = atom_getsym(source)->s_name;
                CMTimeScale value = 1;
                CMTimeValue scale = 0;
                switch (sscanf(string, "%d/%llud", &value, &scale)) {
                    case 2:
                        *result = CMTimeSimplify(CMTimeMake(60 * ABS(scale), ABS(value)));
                        return true;
                    default:
                        *result = kCMTimeInvalid;
                        error("[%s] invalid format %s",
                              class->c_sym->s_name,
                              string);
                        break;
                }
                break;
            default:
                *result = kCMTimeInvalid;
                break;
            }
        }
    return false;
}

C74_HIDDEN bool const CMTimeMakeWithAtomAsSecond(t_atom const * const source, CMTime * const result) {
    if ( result )
        switch ( atom_gettype(source) ) {
            case A_LONG:
                *result = CMTimeMake((CMTimeValue const)atom_getlong(source), 1);
                return true;
            case A_FLOAT:
                *result = CMTimeMakeWithRationalNumber(RationalMakeWithReal(atom_getfloat(source)));
                return true;
            case A_SYM: {
                char const * const string = atom_getsym(source)->s_name;
                CMTimeValue value = 0;
                CMTimeScale scale = 1;
                switch (sscanf(string, "%lld/%ud", &value, &scale)) {
                    case 2:
                        *result = CMTimeSimplify(CMTimeMake(value, scale));
                        return true;
                    default:
                        *result = kCMTimeInvalid;
                        error("[%s] invalid format %s",
                              class->c_sym->s_name,
                              string);
                        break;
                }
                break;
            default:
                *result = kCMTimeInvalid;
                break;
            }
        }
    return false;
}

typedef struct {
    t_object const super;
    CMTimebaseRef const clock;
    t_atom_long const count;
    dispatch_source_t const * const tasks;
    t_outlet const * const pulse;
    t_outlet const * const state;
    CMTime const * const epoch;
    CMTime const limit;
    CMTime check;
    t_atom_long trace;
} t_timecode;

C74_HIDDEN short const __parse__(CMTime * const target, short const argc, t_atom const * const argv) {
    CMTime cached = {0};
    short cursor = 0;
    for ( register short k = 0, K = argc ; k < K ; ++ k )
        if ( CMTimeMakeWithAtomAsBPM(argv + k, &cached) )
            target[cursor++] = cached;
    return cursor;
}

C74_HIDDEN void __fire__(t_timecode const * const this) {
    *(CMTime*const)&this->check = CMTimeAdd(CMTimeMultiplyByRatio(this->limit, 1, 2), CMTimebaseGetTime(this->clock));
    for ( register t_atom_long k = 0, K = this->count ; k < K ; ++ k )
        CMTimebaseSetTimerDispatchSourceNextFireTime(this->clock, this->tasks[k], CMTimeMultiply(this->epoch[k], 1 + CMTimeDiv(this->check, this->epoch[k])), 0);
    outlet_bang((t_outlet*const)this->pulse);
}

C74_HIDDEN t_timecode const * const __new__(t_symbol const * const symbol, short const argc, t_atom const * const argv) {
    
    t_timecode const*const this = (t_timecode*const)object_alloc((t_class*const)class);
    
    if ( this ) switch (CMTimebaseCreateWithSourceTimebase(NULL, prime, (CMTimebaseRef*)&this->clock)) {
            
        case 0:
            
            *(CMTime const**const)&this->epoch = (CMTime const*const)sysmem_newptrclear(argc * sizeof(CMTime));
            *(t_atom_long*const)&this->count = __parse__((CMTime*const)this->epoch, argc, argv);
            *(CMTime const**const)&this->epoch = (CMTime const*const)sysmem_resizeptr((void*const)this->epoch, this->count * sizeof(CMTime));
            
            *(CMTime*const)&this->limit = CMTimeMake(1, 1024);
            
            *(t_outlet const**const)&this->pulse = bangout((void*const)this);
            
            *(dispatch_source_t const**const)&this->tasks = (dispatch_source_t const*const)sysmem_newptr((this->count + 1) * sizeof(dispatch_source_t));
            
            for ( register t_atom_long k = 0, K = this->count ; k < K ; ++ k ) {
                
                CMTime const epoch = this->epoch[K - k - 1];
                
                t_outlet const * const pulse = intout((void*const)this);
                
                dispatch_source_t const source = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
                
                dispatch_source_set_event_handler(source, ^{
                    t_atom_long const count = CMTimeDiv(CMTimebaseGetTime(this->clock), epoch);
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
            break;
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
                        if ( 3 < this->trace )
                            post("[%s] bound", class->c_sym->s_name);
                        break;
                    default:
                        error("bind error");
                        break;
                }
            });
            dispatch_source_set_event_handler(tasks, ^{
                int const socket = (int const)dispatch_source_get_handle(tasks);
                CMTime const buff[4] = {
                    kCMTimeInvalid,
                    kCMTimeInvalid,
                    CMTimebaseGetTime(this->clock),
                    this->check
                };
                struct sockaddr_in target = {0};
                socklen_t length = sizeof(struct sockaddr_in);
                switch (recvfrom(socket, (void*const)buff, 2 * sizeof(CMTime), 0, (struct sockaddr*const)&target, &length)) {
                    case 2 * sizeof(CMTime):
                        switch (sendto(socket, buff, 4 * sizeof(CMTime), 0, (struct sockaddr*const)&target, length)) {
                            case 4 * sizeof(CMTime):
                                if ( 2 < this->trace ) {
                                    char const name[NI_MAXHOST] = {0};
                                    switch ( getnameinfo((struct sockaddr*const)&target, sizeof(struct sockaddr), (char*const)name, sizeof(name), NULL, 0, 0) ) {
                                        case 0:
                                            post("[%s] send time: %lld/%d to %d of %s",
                                                 class->c_sym->s_name,
                                                 buff[2].value, buff[2].timescale,
                                                 ntohs(target.sin_port),
                                                 name);
                                            break;
                                        default:
                                            post("[%s] send time: %lld/%d to %d",
                                                 class->c_sym->s_name,
                                                 buff[2].value, buff[2].timescale,
                                                 ntohs(target.sin_port));
                                            break;
                                    }
                                }
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
                if ( 3 < this->trace )
                    post("[%s] server cancel",
                         class->c_sym->s_name);
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
                        if ( 2 < this->trace ) {
                            char const name[NI_MAXHOST] = {0};
                            switch ( getnameinfo((struct sockaddr const*const)&target, sizeof(struct sockaddr_in), (char*const)name, sizeof(name), NULL, 0, 0) ) {
                                case 0:
                                    post("[%s] send time: %lld/%d, host: %lld/%d to %d of %s",
                                         class->c_sym->s_name,
                                         buff[1].value, buff[1].timescale,
                                         buff[0].value, buff[0].timescale,
                                         ntohs(target.sin_port),
                                         name);
                                    break;
                                default:
                                    post("[%s] send time: %lld/%d, host: %lld/%d to %d",
                                         class->c_sym->s_name,
                                         buff[1].value, buff[1].timescale,
                                         buff[0].value, buff[0].timescale,
                                         ntohs(target.sin_port));
                                    break;
                            }
                        }
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
                if ( 3 < this->trace )
                    post("[%s] client regist",
                         class->c_sym->s_name);
            });
            dispatch_source_set_event_handler(tasks, ^{
                CMTime const buff[6] = {
                    /*0*/kCMTimeInvalid,// self prime clock at SEND
                    /*1*/kCMTimeInvalid,// self this->clock at SEND
                    /*2*/kCMTimeInvalid,// peer clock time
                    /*3*/kCMTimeInvalid,// peer signature
                    /*4*/CMTimebaseGetTime(prime),       // self prime clock at RECV
                    /*5*/CMTimebaseGetTime(this->clock), // self this->clock at RECV
                };
                switch (recv((int const)dispatch_source_get_handle(tasks), (void*const)buff, 4 * sizeof(CMTime), 0)) {
                    case 4 * sizeof(CMTime):
                        if ( CMTimeCompare(marked, buff[3]) ) {
                            length = kCMTimePositiveInfinity;
                            marked = buff[3];
                            if ( 1 < this->trace )
                                post("[%s] sync initialized",
                                     class->c_sym->s_name);
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
                            CMTime const travel = CMTimeSubtract(buff[4], buff[0]);
                            switch ( CMTimeCompare(travel, length) ) {
                                case -1:
                                    length = travel;
                                    anchor.peer = peer.time;
                                    anchor.self = self.base;
                                    if ( 1 < this->trace )
                                        post("[%s] anchor, peer: %lld/%d, host: %lld/%d",
                                             class->c_sym->s_name,
                                             anchor.peer.value, anchor.peer.timescale,
                                             anchor.self.value, anchor.self.timescale);
                                    break;
                                default:
                                    if ( rand() < exp ( CMTimeGetSeconds(travel) / CMTimeGetSeconds(length) - 1 ) * RAND_MAX )
                                        switch (CMTimeCompare(this->limit, CMTimeAbsoluteValue(CMTimeSubtract(self.time, peer.time)))) {
                                            case -1:
                                                CMTimebaseSetRateAndAnchorTime(this->clock,
                                                                               RationalToReal(RationalSimplify(RationalDiv(
                                                                                                                           RationalSimplify(RationalSub(RationalMakeWithCMTime(peer.time), RationalMakeWithCMTime(anchor.peer))),
                                                                                                                           RationalSimplify(RationalSub(RationalMakeWithCMTime(self.base), RationalMakeWithCMTime(anchor.self)))
                                                                                                                           ))),
                                                                               peer.time,
                                                                               self.base);
                                                __fire__(this);
                                                if ( 0 < this->trace )
                                                    post("[%s] rate: %lf, time: %lld/%d, host: %lld/%d, from: %lld/%d",
                                                         class->c_sym->s_name,
                                                         RationalToReal(RationalSimplify(RationalDiv(
                                                                                                     RationalSimplify(RationalSub(RationalMakeWithCMTime(peer.time), RationalMakeWithCMTime(anchor.peer))),
                                                                                                     RationalSimplify(RationalSub(RationalMakeWithCMTime(self.base), RationalMakeWithCMTime(anchor.self)))
                                                                                                     ))),
                                                         peer.time.value, peer.time.timescale,
                                                         self.base.value, self.base.timescale,
                                                         self.time.value, self.time.timescale);
                                                break;
                                            default:
                                                if ( 1 < this->trace )
                                                    post("[%s] %lld/%d is less",
                                                         class->c_sym->s_name,
                                                         CMTimeSubtract(self.time, peer.time).value,
                                                         CMTimeSubtract(self.time, peer.time).timescale);
                                        }
                                    else if ( 2 < this->trace )
                                        post("[%s] unreliable response", class->c_sym->s_name);
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
                if ( 3 < this->trace )
                    post("[%s] client cancel",
                         class->c_sym->s_name);
            });
            dispatch_resume(tasks);
            *(dispatch_source_t*const)(this->tasks + this->count) = tasks;
            break;
        }
        default:
            error("not allowed");
            break;
    }
}

C74_HIDDEN void __rate__(t_timecode const * const this, t_atom_float const value) {
    __remove__(this);
    CMTimebaseSetRate(this->clock, value);
    __fire__(this);
}

C74_HIDDEN void __time__(t_timecode const * const this, t_symbol const * const symbol, short const argc, t_atom const * const argv) {
    CMTime value = kCMTimeInvalid;
    if ( argc == 1 && CMTimeMakeWithAtomAsSecond(argv, &value) ) {
        __remove__(this);
        CMTimebaseSetTime(this->clock, value);
        __fire__(this);
    } else
        error("[%s] %s message can contain single integer, real or rational number",
              class->c_sym->s_name,
              symbol->s_name);
}

C74_HIDDEN void __del__(t_timecode const * const this) {
    for ( register t_atom_long k = 0, K = this->count ; k < K ; ++ k ) {
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

C74_HIDDEN void __info__(t_timecode const * const this, t_atom_long const arg) {
    if ( (*(t_atom_long*const)&this->trace = arg) )
        post("[%s] log level %d",
             class->c_sym->s_name,
             this->trace);
}

C74_HIDDEN void __note__(t_timecode const * const this, void const * const b, long const m, long const a, char * const s) {
    if ( m == ASSIST_INLET )
        sprintf_tr(s, "\
bang: output the elapsed time\r\n\
rate [REAL]: set clock rate, which removes sync state\r\n\
time [REAL]: set clock time, which removes sync state\r\n\
info [INTEGER]: set log trace level, 0 is quiet\r\n\
sync [INTEGER]: export the clock via UDP (INTEGER)\r\n\
sync [INTEGER] [SYMBOL]: import the clock from (SYMBOL):(INTEGER)");
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
                error("[%s] clock error",
                      class->c_sym->s_name);
                break;
        }
    }
    if ( !class ) {
        class = class_new("timecode", (method const)__new__, (method const)__del__, sizeof(t_timecode), NULL, A_GIMME, 0);
        class_addmethod((t_class*const)class, (method const)__bang__, "bang", 0);
        class_addmethod((t_class*const)class, (method const)__info__, "info", A_DEFLONG, 0);
        class_addmethod((t_class*const)class, (method const)__rate__, "rate", A_FLOAT, 0);
        class_addmethod((t_class*const)class, (method const)__time__, "time", A_GIMME, 0);
        class_addmethod((t_class*const)class, (method const)__sync__, "sync", A_GIMME, 0);
        class_addmethod((t_class*const)class, (method const)__note__, "assist", A_CANT, 0);
        class_register(CLASS_BOX, (t_class*const)class);
    }
}
