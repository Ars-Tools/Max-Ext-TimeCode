#include"ext.h"            // standard Max include, always required (except in Jitter)
#include<CoreMedia/CoreMedia.h>

extern void const*const core_new(void const*const, void(*)(void const*const, CMTime), void(*)(void const*const, CMTime, char const*const));
extern void core_free(void const*const);

extern Float64 const core_getrate(void const*const);
extern CMTime const core_gettime(void const*const);

extern void core_setrate(void const*const, Float64 const);
extern void core_settime(void const*const, CMTime const);

extern void core_single(void const * const);
extern void core_server(void const * const, uint16_t const);
extern void core_client(void const * const, uint16_t const, int8_t const * const);
extern void core_version_string(char * const string);
extern size_t core_version_length();

extern void core_tick(void const*const, int8_t const*const, CMTime const);

C74_HIDDEN static t_class const * class = NULL;

C74_HIDDEN CMTime const CMTimeMakeWithReal(long double const real) {
	__int128_t const N = 1ULL << DBL_MANT_DIG;
	long double rest, frac = modfl(real, &rest);
	long double eps = fabsl(real) * FLT_EPSILON;
	if ( !frac ) {
		return CMTimeMake(rest, 1);
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
		if ( fabsl( e ) < eps ) {
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
		return CMTimeMake(( a + c ) + rest * ( b + d ), ( b + d ));
	else if ( b < d )
		return CMTimeMake(c + rest * d, d);
	else if ( b > d )
		return CMTimeMake(a + rest * b, b);
	else
		return CMTimeMake((( a + c ) + rest * ( b + d ) ) / 2, ( b + d ) / 2);
}

typedef struct {
	t_object const super;
	void const * const core;
	t_outlet const * const outlet[3];
} t_timecode;

C74_HIDDEN void __bang__(t_timecode const * const this) {
	CMTime const time = core_gettime(this->core);
	t_atom list[2] = {0};
	atom_setlong(list + 0, time.value);
	atom_setlong(list + 1, time.timescale);
	outlet_list((t_outlet*const)this->outlet[0], gensym("list"), 2, list);
}

C74_HIDDEN void __fire__(t_timecode const * const this, char const * const name, uint64_t const ramp) {
	t_atom list[2] = {0};
	atom_setsym(list + 0, gensym(name));
	atom_setlong(list + 1, ramp);
	outlet_list((t_outlet*const)this->outlet[1], gensym("list"), 2, list);
}

C74_HIDDEN void __info__(t_timecode const * const this, CMTime const time) {
	t_atom list[2] = {0};
	atom_setlong(list + 0, time.value);
	atom_setlong(list + 1, time.timescale);
	outlet_list((t_outlet*const)this->outlet[2], gensym("list"), 2, list);
}

C74_HIDDEN t_timecode const * const __new__(t_symbol const * const symbol, ushort const argc, t_atom const * const argv) {
	t_timecode const * const object = object_alloc((t_class*const)class);
	if (object) {
		*(t_outlet const**const)(object->outlet + 2) = listout((t_object*const)object);
		*(t_outlet const**const)(object->outlet + 1) = listout((t_object*const)object);
		*(t_outlet const**const)(object->outlet + 0) = listout((t_object*const)object);
		*(void**const)&object->core = core_new(object, (void(*)(void const*const, CMTime))__fire__, (void(*)(void const*const, CMTime, char const*const))__info__);
	}
	return object;
}

C74_HIDDEN void __del__(t_timecode const * const this) {
	if (this->core)
		core_free(this->core);
}

C74_HIDDEN void __rate__(t_timecode const * const this, t_atom_float const rate) {
	if (this->core)
		core_setrate(this->core, rate);
}

C74_HIDDEN void __time__(t_timecode const * const this, t_symbol const*const symbol, ushort const argc, t_atom const*const argv) {
	if (this->core) switch (argc) {
		case 0:
			break;
		case 1:
			switch(atom_gettype(argv + 0)) {
				case A_FLOAT:
					core_settime(this->core, CMTimeMakeWithReal(atom_getfloat(argv + 0)));
					break;
				case A_LONG:
					core_settime(this->core, CMTimeMake((CMTimeValue const)atom_getlong(argv + 0), 1));
					break;
				default:
					object_error(this->core, "Invalid message");
					break;
			}
			break;
		case 2:
			if (atom_gettype(argv + 0) == A_LONG && atom_gettype(argv + 1) == A_LONG)
				core_settime(this->core, CMTimeMake((CMTimeValue const)atom_getlong(argv + 0), (CMTimeScale const)atom_getlong(argv + 1)));
			else
				object_error(this->core, "Invalid message");
			break;
		default:
			object_error(this->core, "Invalid message");
			break;
	}
}

C74_HIDDEN void __sync__(t_timecode const * const this, t_symbol const*const symbol, ushort const argc, t_atom const * const argv) {
	if (this->core) switch (argc) {
		case 0:
			core_single(this->core);
			break;
		case 1:
			if (atom_gettype(argv + 0) == A_LONG)
				core_server(this->core, atom_getlong(argv + 0));
			else
				object_error(this, "Invalid message");
			break;
		case 2:
			if (atom_gettype(argv + 0) == A_LONG && atom_gettype(argv + 1) == A_SYM)
				core_client(this->core, atom_getlong(argv + 0), atom_getsym(argv + 1)->s_name);
			else
				object_error(this, "Invalid message");
			break;
		default:
			object_error(this, "Invalid message");
			break;
	}
}

C74_HIDDEN void __tick__(t_timecode const * const this, t_symbol const * const symbol, ushort const argc, t_atom const * const argv) {
	if (this->core) switch (argc) {
		case 1:
			if (atom_gettype(argv + 0) != A_SYM)
				object_error(this, "first argument should be symbol");
			else
				core_tick(this->core, atom_getsym(argv + 0)->s_name, kCMTimeZero);
			break;
		case 2:
			if (atom_gettype(argv + 0) != A_SYM)
				object_error(this, "first argument should be symbol");
			else if (atom_gettype(argv + 1) == A_FLOAT)
				core_tick(this->core, atom_getsym(argv + 0)->s_name, CMTimeMakeWithReal(atom_getfloat(argv + 1)));
			else if (atom_gettype(argv + 1) == A_LONG)
				core_tick(this->core, atom_getsym(argv + 0)->s_name, CMTimeMake(atom_getlong(argv + 1), 1));
			else if (atom_gettype(argv + 1) == A_SYM) {
				CMTimeValue value = 0;
				CMTimeScale scale = 1;
				switch (sscanf(atom_getsym(argv + 1)->s_name, "%lld/%u", &value, &scale)) {
					case 2:
						core_tick(this->core, atom_getsym(argv + 0)->s_name, CMTimeMake(value, scale));
						break;
					default:
						object_error(this, "rational format should be [integer]/[integer]");
						break;
				}
			}
			else
				object_error(this, "invalid message");
			break;
		case 3:
			if (atom_gettype(argv + 0) != A_SYM)
				object_error(this, "first argument should be symbol");
			else if (atom_gettype(argv + 1) != A_LONG)
				object_error(this, "second argument should be integer");
			else if (atom_gettype(argv + 2) != A_LONG)
				object_error(this, "third argument should be integer");
			else
				core_tick(this->core, atom_getsym(argv + 0)->s_name, CMTimeMake(atom_getlong(argv + 1), atom_getlong(argv + 2)));
			break;
		default:
			object_error(this, "invalid message");
	}
}

C74_EXPORT void ext_main(void * const _) {
	if ((class = class_new("timecode", (method const)__new__, (method const)__del__, sizeof(t_timecode), NULL, 0))) {
		char * const string = (char * const)alloca(core_version_length() + 1);
		core_version_string(string);
		post("timecode version %s", string);
		class_addmethod((t_class*const)class, (method const)__bang__, "bang", 0);
		class_addmethod((t_class*const)class, (method const)__rate__, "rate", A_FLOAT, 0);
		class_addmethod((t_class*const)class, (method const)__time__, "time", A_GIMME, 0);
		class_addmethod((t_class*const)class, (method const)__sync__, "sync", A_GIMME, 0);
		class_addmethod((t_class*const)class, (method const)__tick__, "tick", A_GIMME, 0);
		class_register(CLASS_BOX, (t_class*const)class);
	}
}
