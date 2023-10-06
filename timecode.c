#include"ext.h"            // standard Max include, always required (except in Jitter)
#include<netdb.h>
#include<arpa/inet.h>
#include<CoreMedia/CoreMedia.h>

C74_HIDDEN static t_class const * class = NULL;
C74_HIDDEN static CMTimebaseRef prime = NULL;
C74_HIDDEN static dispatch_queue_t queue = NULL;

C74_HIDDEN in_addr_t const addr(char const * const host) {
    in_addr_t addr = {0};
    inet_pton(AF_INET, host, &addr);
    return addr;
}

C74_HIDDEN __uint128_t const gcd(__uint128_t const x, __uint128_t const y) {
    return y ? gcd(y, x % y) : x;
}

/* rational number type */

typedef struct {
    __int128_t const p;
    __int128_t const q;
} rational256_t;

#define rational_make(x, y) ((rational256_t const) { .p = x, .q = y })

#define rational_is_normal(x) (!!x.q)

#define rational_to_real(x) ((long double)x.p / (long double)x.q)

#define rational_integral(x) (x.p / x.q)

#define rational_fraction(x) ((rational256_t const) { .p = x.p % x.q, .q = x.q })

C74_HIDDEN rational256_t const rational_normalize(rational256_t const number) {
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

C74_HIDDEN rational256_t const rational_simplify(rational256_t const number) {
    rational256_t const normal = rational_normalize(number);
    if ( normal.q ) {
        assert(0 < normal.q);
        __int128_t const divisor = gcd(ABS(normal.p), normal.q);
        return (rational256_t const) {
            .p = normal.p / divisor,
            .q = normal.q / divisor
        };
    }
    else
        return normal;
}

C74_HIDDEN rational256_t const rational_neg(rational256_t const r) {
    return (rational256_t const) {
        .p = -r.p,
        .q =  r.q
    };
}

C74_HIDDEN rational256_t const rational_add(rational256_t const x, rational256_t const y) {
    return (rational256_t const) {
        .p = ( x.p * y.q ) + ( y.p * x.q ),
        .q = x.q * y.q
    };
}

C74_HIDDEN rational256_t const rational_sub(rational256_t const x, rational256_t const y) {
    return (rational256_t const) {
        .p = ( x.p * y.q ) - ( y.p * x.q ),
        .q = x.q * y.q
    };
}

C74_HIDDEN rational256_t const rational_mul(rational256_t const x, rational256_t const y) {
    return (rational256_t const) {
        .p = x.p * y.p,
        .q = x.q * y.q
    };
}

C74_HIDDEN rational256_t const rational_div(rational256_t const x, rational256_t const y) {
    return (rational256_t const) {
        .p = x.p * y.q,
        .q = x.q * y.p
    };
}

C74_HIDDEN rational256_t const rational_mod(rational256_t const x, rational256_t const y) {
    return (rational256_t const) {
        .p = ( x.p * y.q ) % ( y.p * x.q ),
        .q = ( x.q * y.q )
    };
}

C74_HIDDEN rational256_t const rational_make_with_real(long double const real) {
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
    while ( ( b <= N ) && ( d <= N ) ) {
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
            .p = ( ( a + c ) + rest * ( b + d ) ) / 2,
            .q = ( b + d ) / 2
        };
}

#define rational_make_with_CMTime(x) ((rational256_t const){.p = x.value, .q = (CMTimeScale const)x.timescale })

C74_HIDDEN CMTime const CMTimeMakeWithRationalNumber(rational256_t const number) {
    rational256_t const value = rational_simplify(number);
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
                *result = CMTimeMakeWithRationalNumber(rational_div(rational_make(60, 1), rational_make_with_real(fabs(atom_getfloat(source)))));
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
                *result = CMTimeMakeWithRationalNumber(rational_make_with_real(atom_getfloat(source)));
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

/* timecode object type */

typedef struct {
    t_object const super;
    t_atom_long const count;
    CMTimebaseRef const * const clock;
    dispatch_source_t const * const tasks;
    t_outlet * const pulse;
    t_outlet * const state;
    CMTime * const epoch;
    CMTime limit;
    CMTime check;
    CMTime token;
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

C74_HIDDEN void __fire__(t_timecode const * const this, t_atom_long const index) {
    assert( 0 <= index );
    assert( index < this->count );
    switch (CMTimebaseSetTimerDispatchSourceNextFireTime(this->clock[index], this->tasks[index], CMTimeMultiply(this->epoch[index], 1 + CMTimeDiv(CMTimebaseGetTime(this->clock[index]), this->epoch[index])), 0)) {
        case noErr:
            break;
    }
}

C74_HIDDEN void __fire__all__(t_timecode const * const this) {
    for ( register t_atom_long k = 0, K = this->count ; k < K ; ++ k )
        __fire__(this, k);
    outlet_bang(this->pulse);
}

C74_HIDDEN void __beat__(t_timecode const * const this, t_symbol const * const symbol, short const argc, t_atom const * const argv) {
    long const index = proxy_getinlet((t_object*const)this);
    CMTime value = {0};
    switch ( index ) {
        case 0:
            object_error((t_object*const)this, "primary inlet cannot accept beat message");
            break;
        default:
            switch ( argc ) {
                case 1:
                    if (CMTimeMakeWithAtomAsBPM(argv, &value))
                        this->epoch[index-1] = value;
                    break;
                default:
                    break;
            }
    }
}

C74_HIDDEN t_timecode const * const __new__(t_symbol const * const symbol, short const argc, t_atom * const argv) {
    
    t_timecode * const this = (t_timecode*const)object_alloc((t_class*const)class);
    
    if ( this ) {
        
        *(CMTime const**const)&this->epoch = (CMTime*const)sysmem_newptr(argc * sizeof(CMTime));
        
        *(t_atom_long*const)&this->count = __parse__(this->epoch, attr_args_offset(argc, argv), argv);
        
        *(CMTime const**const)&this->epoch = (CMTime*const)sysmem_resizeptr(this->epoch, this->count * sizeof(CMTime));
        
        *(CMTimebaseRef const**const)&this->clock = (CMTimebaseRef const*const)sysmem_newptr( ( this->count + 1 ) * sizeof(CMTimebaseRef));
        
        *(dispatch_source_t const**const)&this->tasks = (dispatch_source_t*const)sysmem_newptr( ( this->count + 1 ) * sizeof(dispatch_source_t));
        
        *(t_outlet const**const)&this->pulse = bangout(this);
        
        if ( CMTimebaseCreateWithSourceTimebase(NULL, prime, (CMTimebaseRef*const)this->clock + this->count) )
            object_error((t_object*const)this, "timebase error");
        
        else {
            
            this->limit = CMTimeMake(1, 1024);
            this->check = CMTimeMake(1, 1);
            
            attr_args_process(this, argc, argv);
            
            switch (CMTimebaseSetRate(this->clock[this->count], 1)) {
                case noErr:
                    break;
            }
            
            if ( this->count ) {
                
                object_addmethod((t_object*const)this, (method const)__beat__, "beat", A_GIMME, 0);
                
                for ( register t_atom_long k = this->count - 1 ; 0 <= k ; --k ) {
                    
                    if ( CMTimebaseCreateWithSourceTimebase(NULL, this->clock[this->count], (CMTimebaseRef*const)this->clock + k) )
                        object_error((t_object*const)this, "timebase error");
                    
                    else {
                        
                        CMTimebaseRef const clock = (CMTimebaseRef const)CFRetain(this->clock[k]);
                        dispatch_source_t const timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
                        
                        switch (CMTimebaseSetRateAndAnchorTime(clock, 1, kCMTimeZero, kCMTimeZero)) {
                            case noErr:
                                break;
                        }
                        
                        proxy_new_forinlet((t_object*const)this, k + 1, NULL, inlet_new(this, NULL));
                        t_outlet * const count = intout(this);
                        
                        dispatch_source_set_registration_handler(timer, ^{
                            CMTime const epoch = this->epoch[k];
                            t_atom_long const cycle = CMTimeDiv(CMTimebaseGetTime(clock), epoch);
                            CMTimebaseAddTimerDispatchSource(clock, timer);
                            CMTimebaseSetTimerDispatchSourceNextFireTime(clock, timer, CMTimeMultiply(epoch, 1 + (CMTimeScale const)cycle), 0);
                        });
                        
                        dispatch_source_set_event_handler(timer, ^{
                            CMTime const epoch = this->epoch[k];
                            t_atom_long const cycle = CMTimeDiv(CMTimebaseGetTime(clock), epoch);
                            outlet_int(count, cycle);
                            CMTimebaseSetTimerDispatchSourceNextFireTime(clock, timer, CMTimeMultiply(epoch, 1 + (CMTimeScale const)cycle), 0);
                        });
                        
                        dispatch_source_set_cancel_handler(timer, ^{
                            CMTimebaseRemoveTimerDispatchSource(clock, timer);
                            CFRelease(clock);
                        });
                        
                        dispatch_resume((*(dispatch_source_t*const)(this->tasks + k) = timer));
                        
                    }
                }
            }
        }
        
        *(dispatch_source_t*const)(this->tasks + this->count) = NULL;
        
        *(t_outlet**const)&this->state = listout(this);
        
    }
    return this;
}

C74_HIDDEN void __bang__(t_timecode const * const this) {
    long const index = proxy_getinlet((t_object*const)this);
    if ( index )
        __fire__(this, index - 1);
    else {
        CMTime const time = CMTimebaseGetTime(this->clock[this->count]);
        t_atom vs[2] = {0};
        atom_setlong(vs + 0, (t_atom_long const)time.value);
        atom_setlong(vs + 1, (t_atom_long const)time.timescale);
        outlet_list((t_outlet*const)this->state, gensym("list"), 2, vs);
    }
}

C74_HIDDEN void __remove__(t_timecode const * const this) {
    if ( this->tasks[this->count] ) {
        dispatch_source_cancel(this->tasks[this->count]);
        dispatch_release(this->tasks[this->count]);
        *(dispatch_source_t*const)(this->tasks + this->count) = NULL;
    }
}

C74_HIDDEN void __export__(t_timecode const * const this, struct sockaddr_in const target) {
    dispatch_source_t const tasks = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, (uintptr_t const)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), 0, queue);
    dispatch_source_set_registration_handler(tasks, ^{
        int const socket = (int const)dispatch_source_get_handle(tasks);
        switch (bind(socket, (struct sockaddr*const)&target, sizeof(struct sockaddr_in))) {
            case 0:
                if ( 3 < this->trace )
                    object_post((t_object*const)this, "bound");
                break;
            default:
                object_error((t_object*const)this, "bind error");
                break;
        }
    });
    dispatch_source_set_event_handler(tasks, ^{
        int const socket = (int const)dispatch_source_get_handle(tasks);
        CMTime const buff[4] = {
            kCMTimeInvalid,
            kCMTimeInvalid,
            CMTimebaseGetTime(this->clock[this->count]),
            this->token,
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
                                    object_post((t_object*const)this, "send time: %ld/%d to %d of %s",
                                                buff[2].value, buff[2].timescale,
                                                ntohs(target.sin_port),
                                                name);
                                    break;
                                default:
                                    object_post((t_object*const)this, "send time: %ld/%d to %d",
                                                buff[2].value, buff[2].timescale,
                                                ntohs(target.sin_port));
                                    break;
                            }
                        }
                        break;
                    default:
                        object_error((t_object*const)this, "send error");
                        break;
                }
                break;
            default:
                object_error((t_object*const)this, "recv error");
                break;
        }
    });
    dispatch_source_set_cancel_handler(tasks, ^{
        close((int const)dispatch_source_get_handle(tasks));
        if ( 3 < this->trace )
            object_post((t_object*const)this, "server cancel");
    });
    dispatch_resume((*(dispatch_source_t*const)(this->tasks + this->count) = tasks));
}

C74_HIDDEN void __import__(t_timecode const * const this, struct sockaddr_in const target) {
    __block struct {
        CMTime self;
        CMTime peer;
    } anchor = {
        .self = kCMTimeInvalid,
        .peer = kCMTimeInvalid,
    };
    __block CMTime marked = kCMTimeInvalid;
    __block CMTime length = kCMTimeInvalid;
    
    dispatch_source_t const timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
    dispatch_source_t const tasks = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, (uintptr_t const)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), 0, queue);
    dispatch_source_set_event_handler(timer, ^{
        CMTime const buff[2] = {
            CMTimebaseGetTime(prime),
            CMTimebaseGetTime(this->clock[this->count])
        };
        switch (sendto((int const)dispatch_source_get_handle(tasks), buff, 2 * sizeof(CMTime), 0, (struct sockaddr*const)&target, sizeof(struct sockaddr_in))) {
            case 2 * sizeof(CMTime):
                if ( 2 < this->trace ) {
                    char const name[NI_MAXHOST] = {0};
                    switch ( getnameinfo((struct sockaddr const*const)&target, sizeof(struct sockaddr_in), (char*const)name, sizeof(name), NULL, 0, 0) ) {
                        case 0:
                            object_post((t_object*const)this,
                                        "send time: %lld/%d, host: %lld/%d to %d of %s",
                                        buff[1].value, buff[1].timescale,
                                        buff[0].value, buff[0].timescale,
                                        ntohs(target.sin_port),
                                        name);
                            break;
                        default:
                            object_post((t_object*const)this,
                                        "send time: %lld/%d, host: %lld/%d to %d",
                                        buff[1].value, buff[1].timescale,
                                        buff[0].value, buff[0].timescale,
                                        ntohs(target.sin_port));
                            break;
                    }
                }
                break;
            default:
                object_error((t_object*const)this, "send error");
                break;
        }
        switch (CMTimebaseSetTimerDispatchSourceNextFireTime(prime, timer, CMTimeMultiply(this->check, 1 + CMTimeDiv(buff[0], this->check)), 0)) {
            case noErr:
                break;
        }
    });
    dispatch_source_set_registration_handler(tasks, ^{
        dispatch_resume(timer);
        CMTimebaseAddTimerDispatchSource(prime, timer);
        CMTimebaseSetTimerDispatchSourceToFireImmediately(prime, timer);
        if ( 3 < this->trace )
            object_post((t_object*const)this, "client regist");
    });
    dispatch_source_set_event_handler(tasks, ^{
        CMTime const buff[6] = {
            /*0*/kCMTimeInvalid,// self prime clock at SEND
            /*1*/kCMTimeInvalid,// self this->clock at SEND
            /*2*/kCMTimeInvalid,// peer clock time
            /*3*/kCMTimeInvalid,// peer signature
            /*4*/CMTimebaseGetTime(prime),       // self prime clock at RECV
            /*5*/CMTimebaseGetTime(this->clock[this->count]), // self this->clock at RECV
        };
        switch (recv((int const)dispatch_source_get_handle(tasks), (void*const)buff, 4 * sizeof(CMTime), 0)) {
            case 4 * sizeof(CMTime):
                if ( CMTimeCompare(marked, buff[3]) ) {
                    length = kCMTimePositiveInfinity;
                    marked = buff[3];
                    if ( 1 < this->trace )
                        object_post((t_object*const)this, "sync initialized");
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
                                object_post((t_object*const)this, "anchor, peer: %lld/%d, host: %lld/%d",
                                            anchor.peer.value, anchor.peer.timescale,
                                            anchor.self.value, anchor.self.timescale);
                            break;
                        default:
                            if ( CMTimeGetSeconds(travel) * rand() < CMTimeGetSeconds(length) * RAND_MAX )
                                switch (CMTimeCompare(this->limit, CMTimeAbsoluteValue(CMTimeSubtract(self.time, peer.time)))) {
                                    case -1:
                                        CMTimebaseSetRateAndAnchorTime(this->clock[this->count],
                                                                       rational_to_real(rational_simplify(rational_div(rational_sub(rational_make_with_CMTime(peer.time), rational_make_with_CMTime(anchor.peer)),
                                                                                                                       rational_sub(rational_make_with_CMTime(self.base), rational_make_with_CMTime(anchor.self))))),
                                                                       peer.time,
                                                                       self.base);
                                        __fire__all__(this);
                                        if ( 0 < this->trace )
                                            object_post((t_object*const)this,
                                                        "rate: %lf, time: %lld/%d, host: %lld/%d, from: %lld/%d",
                                                        rational_to_real(rational_simplify(rational_div(rational_sub(rational_make_with_CMTime(peer.time), rational_make_with_CMTime(anchor.peer)),
                                                                                                 rational_sub(rational_make_with_CMTime(self.base), rational_make_with_CMTime(anchor.self))))),
                                                        peer.time.value, peer.time.timescale,
                                                        self.base.value, self.base.timescale,
                                                        self.time.value, self.time.timescale);
                                        break;
                                    default:
                                        if ( 1 < this->trace )
                                            object_post((t_object*const)this,
                                                        "%lld/%d is less",
                                                        CMTimeSubtract(self.time, peer.time).value,
                                                        CMTimeSubtract(self.time, peer.time).timescale);
                                }
                            else if ( 2 < this->trace )
                                object_post((t_object*const)this, "unreliable response");
                            break;
                    }
                }
                break;
            default:
                object_error((t_object*const)this, "recv error");
                break;
        }
    });
    dispatch_source_set_cancel_handler(tasks, ^{
        CMTimebaseRemoveTimerDispatchSource(prime, timer);
        dispatch_source_cancel(timer);
        dispatch_release(timer);
        if ( 3 < this->trace )
            object_post((t_object*const)this, "client cancel");
    });
    dispatch_resume((*(dispatch_source_t*const)(this->tasks + this->count) = tasks));
}

C74_HIDDEN void __sync__(t_timecode const * const this, t_symbol const * const symbol, short const argc, t_atom const * const argv) {
    if (proxy_getinlet((t_object*const)this))
        object_error((t_object*const)this, "only primary inlet can accept sync message");
    else switch ( argc ) {
        case 0:
            __remove__(this);
            break;
        case 1:
            __remove__(this);
            __export__(this, (struct sockaddr_in const) {
                .sin_family = AF_INET,
                .sin_addr = {
                    .s_addr = INADDR_ANY
                },
                .sin_port = htons(atom_getlong(argv)),
                .sin_len = sizeof(struct sockaddr_in),
                .sin_zero = {0}
            });
            break;
        case 2:
            __remove__(this);
            __import__(this, (struct sockaddr_in const) {
                .sin_family = AF_INET,
                .sin_addr = {
                    .s_addr = addr(atom_getsym(argv + 1)->s_name)
                },
                .sin_port = htons(atom_getlong(argv + 0)),
                .sin_len = sizeof(struct sockaddr_in),
                .sin_zero = {0}
            });
            break;
        default:
            object_error((t_object*const)this, "not allowed");
            break;
    }
}

C74_HIDDEN void __rate__(t_timecode const * const this, t_atom_float const rate) {
    long const index = proxy_getinlet((t_object*const)this);
    switch (CMTimebaseSetRate(this->clock[index ? index-1 : this->count], rate)) {
        case noErr:
            break;
    }
}

C74_HIDDEN void __time__(t_timecode const * const this, t_symbol const * const symbol, short const argc, t_atom const * const argv) {
    long const index = proxy_getinlet((t_object*const)this);
    CMTime value[2] = {0};
    switch ( argc ) {
        case 1:
            if ( CMTimeMakeWithAtomAsSecond(argv+0, value+0) ) switch ( index ) {
                case 0: switch (CMTimebaseSetTime(this->clock[this->count], value[0])) {
                    case noErr:
                        __remove__(this);
                        __fire__all__(this);
                        break;
                }
                    break;
                default: switch (CMTimebaseSetTime(this->clock[index-1], value[0])) {
                    case noErr:
                        __fire__(this, index-1);
                        break;
                }
                    break;
            }
            else goto recover;
            break;
        case 2:
            if ( CMTimeMakeWithAtomAsSecond(argv+0, value+0) && CMTimeMakeWithAtomAsSecond(argv+1, value+1) ) switch ( index ) {
                case 0: switch (CMTimebaseSetAnchorTime(this->clock[this->count], value[0], value[1])) {
                    case noErr:
                        __remove__(this);
                        __fire__all__(this);
                        break;
                }
                    break;
                default: switch (CMTimebaseSetAnchorTime(this->clock[index-1], value[0], value[1])) {
                    case noErr:
                        __fire__(this, index-1);
                        break;
                }
                    break;
            }
            else goto recover;
            break;
        default:
        recover:
            object_error((t_object*const)this, "%s message can contain single integer, real or rational number", symbol->s_name);
            break;
    }
}

C74_HIDDEN void __del__(t_timecode const * const this) {
    for ( register t_atom_long k = 0, K = this->count + 1 ; k < K ; ++ k ) {
        if ( this->tasks[k] ) {
            dispatch_source_cancel(this->tasks[k]);
            dispatch_release(this->tasks[k]);
        }
        CFRelease(this->clock[k]);
    }
    sysmem_freeptr((void*const)this->tasks);
    sysmem_freeptr((void*const)this->clock);
    sysmem_freeptr((void*const)this->epoch);
}

C74_HIDDEN void __info__(t_timecode * const this, t_atom_long const arg) {
    if ( ( this->trace = arg ) )
        object_post((t_object*const)this, "log level %d", this->trace);
}

C74_HIDDEN void __note__(t_timecode const * const this, void const * const b, long const m, long const a, char * const s) {
    if ( m == ASSIST_INLET )
        sprintf_tr(s, "\
bang: output the elapsed time for primary inlet otherwise fire metronome\r\n\
rate [REAL]: set clock rate, sync state gets removed when primary inlet receives\r\n\
time [INTEGERAL, REAL, RATIONAL]: set clock time, sync state gets removed when primary inlet receives\r\n\
info [INTEGERAL]: set log trace level, 0 is quiet\r\n\
sync [INTEGERAL]: export the clock via UDP (INTEGER)\r\n\
sync [INTEGERAL] [SYMBOL]: import the clock from (SYMBOL):(INTEGER)");
    else if ( a == 0 )
        sprintf_tr(s, "output elapsed time when bang message is received");
    else if ( a == this->count + 1 )
        sprintf_tr(s, "output bang message when the clock is resynchronized");
    else
        sprintf_tr(s, "output elapsed count every %lld/%d (≒%.3lf) second", this->epoch[a-1].value, this->epoch[a-1].timescale, CMTimeGetSeconds(this->epoch[a-1]));
}

C74_HIDDEN t_max_err const __interval__(t_timecode * const this, void * const attr, long const argc, t_atom const * const argv) {
    CMTime check = {0};
    switch ( argc ) {
        case 1:
            if ( CMTimeMakeWithAtomAsSecond(argv, &check) )
                this->check = check;
            return MAX_ERR_NONE;
        default:
            return MAX_ERR_GENERIC;
    }
}

C74_HIDDEN t_max_err const __threshold__(t_timecode * const this, void * const attr, long const argc, t_atom const * const argv) {
    CMTime limit = {0};
    switch ( argc ) {
        case 1:
            if ( CMTimeMakeWithAtomAsSecond(argv, &limit) )
                this->limit = limit;
            return MAX_ERR_NONE;
        default:
            return MAX_ERR_GENERIC;
    }
}

C74_HIDDEN t_max_err const __source__(t_timecode const * const this, t_attr const * const attr, long const argc, char const * const argv) {
    AudioObjectPropertyAddress const address = {
        .mSelector = kAudioHardwarePropertyDevices,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain,
    };
    uint32_t size = 0;
    switch ( AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, NULL, &size) ) {
        case 0:
            break;
        default:
            return MAX_ERR_GENERIC;
    }
    AudioDeviceID * const devices = (AudioDeviceID*const)alloca(size);
    switch ( AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL, &size, devices)) {
        case 0:
            break;
        default:
            return MAX_ERR_GENERIC;
    }
    switch ( argc ) {
        case 1:
            switch ( atom_gettype((t_atom*const)argv) ) {
                case A_SYM:
                    for ( register long k = 0, K = size / sizeof(AudioDeviceID) ; k < K ; ++ k ) {
                        {
                            AudioObjectPropertyAddress const address = {
                                .mSelector = kAudioDevicePropertyStreamConfiguration,
                                .mScope = kAudioObjectPropertyScopeOutput,
                                .mElement = kAudioObjectPropertyElementMain
                            };
                            uint32_t size = 0;
                            switch (AudioObjectGetPropertyDataSize(devices[k], &address, 0, NULL, &size)) {
                                case 0:
                                    break;
                                default:
                                    continue;
                            }
                            AudioBufferList * const list = (AudioBufferList*const)alloca(size);
                            switch (AudioObjectGetPropertyData(devices[k], &address, 0, NULL, &size, list)) {
                                case 0:
                                    break;
                                default:
                                    continue;
                            }
                            uint32_t count = 0;
                            for ( register long k = 0, K = list->mNumberBuffers ; k < K ; ++ k )
                                count += list->mBuffers[k].mNumberChannels;
                            if ( !count )
                                continue;;
                        }
                        {
                            AudioObjectPropertyAddress const address = {
                                .mSelector = kAudioDevicePropertyDeviceName,
                                .mScope = kAudioObjectPropertyScopeGlobal,
                                .mElement = kAudioObjectPropertyElementMain
                            };
                            uint32_t size = 0;
                            switch (AudioObjectGetPropertyDataSize(devices[k], &address, 0, NULL, &size)) {
                                case 0:
                                    break;
                                default:
                                    continue;
                            }
                            char * const name = (char*const)alloca(size);
                            switch (AudioObjectGetPropertyData(devices[k], &address, 0, NULL, &size, name)) {
                                case 0:
                                    break;
                                default:
                                    continue;
                            }
                            CMClockRef clock = NULL;
                            if ( !strcmp(name, atom_getsym((t_atom*const)argv)->s_name) )
                                switch (CMAudioDeviceClockCreateFromAudioDeviceID(NULL, devices[k], &clock)) {
                                    case noErr:
                                        switch (CMTimebaseSetSourceClock(this->clock[this->count], clock)) {
                                            case noErr:
                                                return MAX_ERR_NONE;
                                            default:
                                                object_error((t_object*const)this, "source clock wasn't updated");
                                                CFRelease(clock);
                                                continue;;
                                        }
                                    default:
                                        object_error((t_object*const)this, "no clock created");
                                        continue;
                                }
                        }
                    }
            }
        default:
            object_error((t_object*const)this, "choose one from");
            for ( register long k = 0, K = size / sizeof(AudioDeviceID) ; k < K ; ++ k ) {
                {
                    AudioObjectPropertyAddress const address = {
                        .mSelector = kAudioDevicePropertyStreamConfiguration,
                        .mScope = kAudioObjectPropertyScopeOutput,
                        .mElement = kAudioObjectPropertyElementMain
                    };
                    uint32_t size = 0;
                    switch (AudioObjectGetPropertyDataSize(devices[k], &address, 0, NULL, &size)) {
                        case 0:
                            break;
                        default:
                            continue;
                    }
                    AudioBufferList * const list = (AudioBufferList*const)alloca(size);
                    switch (AudioObjectGetPropertyData(devices[k], &address, 0, NULL, &size, list)) {
                        case 0:
                            break;
                        default:
                            continue;
                    }
                    uint32_t count = 0;
                    for ( register long k = 0, K = list->mNumberBuffers ; k < K ; ++ k )
                        count += list->mBuffers[k].mNumberChannels;
                    if ( !count )
                        continue;;
                }
                {
                    AudioObjectPropertyAddress const address = {
                        .mSelector = kAudioDevicePropertyDeviceName,
                        .mScope = kAudioObjectPropertyScopeGlobal,
                        .mElement = kAudioObjectPropertyElementMain
                    };
                    uint32_t size = 0;
                    switch (AudioObjectGetPropertyDataSize(devices[k], &address, 0, NULL, &size)) {
                        case 0:
                            break;
                        default:
                            continue;
                    }
                    char * const name = (char*const)alloca(size);
                    switch (AudioObjectGetPropertyData(devices[k], &address, 0, NULL, &size, name)) {
                        case 0:
                            break;
                        default:
                            continue;
                    }
                    object_post((t_object*const)this, " - %s", name);
                }
            }
            break;
    }
    return MAX_ERR_GENERIC;
}

C74_EXPORT void ext_main(void * const _) {
    if ((class = class_new("timecode", (method const)__new__, (method const)__del__, sizeof(t_timecode), NULL, A_GIMME, 0))) {
        class_addmethod((t_class*const)class, (method const)__bang__, "bang", 0);
        class_addmethod((t_class*const)class, (method const)__info__, "info", A_DEFLONG, 0);
        class_addmethod((t_class*const)class, (method const)__rate__, "rate", A_FLOAT, 0);
        class_addmethod((t_class*const)class, (method const)__time__, "time", A_GIMME, 0);
        class_addmethod((t_class*const)class, (method const)__sync__, "sync", A_GIMME, 0);
        class_addmethod((t_class*const)class, (method const)__note__, "assist", A_CANT, 0);
        class_addattr((t_class*const)class, attribute_new("source", gensym("symbol"), 0, NULL, (method const)__source__));
        class_addattr((t_class*const)class, attribute_new("interval", gensym("float64"), 0, NULL, (method const)__interval__));
        class_addattr((t_class*const)class, attribute_new("threshold", gensym("float64"), 0, NULL, (method const)__threshold__));
        class_register(CLASS_BOX, (t_class*const)class);
        
        queue = dispatch_queue_create("art.xsgn.timecode", DISPATCH_QUEUE_CONCURRENT);
        
        switch (CMTimebaseCreateWithSourceClock(NULL, CMClockGetHostTimeClock(), &prime)) {
            case noErr:
                switch (CMTimebaseSetRateAndAnchorTime(prime, 1, kCMTimeZero, kCMTimeZero)) {
                    case noErr:
                        break;
                    default:
                        error("[%s] critical error", class->c_sym->s_name);
                        break;
                }
                break;
            default:
                error("[%s] clock error", class->c_sym->s_name);
                break;
        }
    }
}
