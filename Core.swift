//
//  Core.swift
//  timecode
//
//  Created by Kota on 10/17/R5.
//
import CoreMedia
import Darwin
extension CMTime {
    var ceil: CMTime {
        .init(value: value - ( value % .init(timescale) ) + .init(timescale), timescale: timescale)
    }
}
extension CMTimebase {
    static func create(sourceClock: CMClock) throws -> CMTimebase {
        var result: CMTimebase?
        CMTimebaseCreateWithSourceClock(allocator: kCFAllocatorDefault, sourceClock: sourceClock, timebaseOut: &result)
        return result!
    }
    static func create(sourceTimebase: CMTimebase) throws -> CMTimebase {
        var result: CMTimebase?
        CMTimebaseCreateWithSourceTimebase(allocator: kCFAllocatorDefault, sourceTimebase: sourceTimebase, timebaseOut: &result)
        return result!
    }
}
/*
extension CMClock {
	static let hostTimeClock = CMClockGetHostTimeClock()
}
extension DispatchSourceTimer {
	var core: dispatch_source_t {
		self as!dispatch_source_t
	}
}
extension CMTimebase {
	func setRate(_ rate: Float64) throws {
		CMTimebaseSetRate(self, rate: rate)
	}
	func setTime(_ time: CMTime) throws {
		CMTimebaseSetTime(self, time: time)
	}
	func setRateAndAnchorTime(rate: Float64, anchorTime: CMTime, referenceTime: CMTime) throws {
		CMTimebaseSetRateAndAnchorTime(self, rate: rate, anchorTime: anchorTime, immediateSourceTime: referenceTime)
	}
	var rate: Float64 {
		CMTimebaseGetRate(self)
	}
	var time: CMTime {
		CMTimebaseGetTime(self)
	}
	func addTimer(_ timerSource: DispatchSourceTimer) throws {
		CMTimebaseAddTimerDispatchSource(self, timerSource: timerSource.core)
	}
	@discardableResult
	func removeTimer(_ timerSource: DispatchSourceTimer) throws -> OSStatus {
		CMTimebaseRemoveTimerDispatchSource(self, timerSource: timerSource.core)
	}
	@discardableResult
	func setTimerToFireImmediately(_ timerSource: DispatchSourceTimer) throws -> OSStatus {
		CMTimebaseSetTimerDispatchSourceToFireImmediately(self, timerSource: timerSource.core)
	}
	@discardableResult
	func setTimerNextFireTime(_ timerSource: DispatchSourceTimer, fireTime: CMTime) throws -> OSStatus {
		CMTimebaseSetTimerDispatchSourceNextFireTime(self, timerSource: timerSource.core, fireTime: fireTime, flags: 0)
	}
}
*/

fileprivate class Core {
    static let clock: CMClock = .hostTimeClock
    static let queue: DispatchQueue = .init(label: "art.xsgn.timecode", attributes: .concurrent)
    static let print = unsafeBitCast(dlsym(dlopen(.none, RTLD_LAZY), "object_post"), to: (@convention(c)(UnsafeRawPointer, UnsafePointer<CChar>) -> Void).self)
    static let error = unsafeBitCast(dlsym(dlopen(.none, RTLD_LAZY), "object_error"), to: (@convention(c)(UnsafeRawPointer, UnsafePointer<CChar>) -> Void).self)
    enum Mode {
        case None
        case Server(port: UInt16)
        case Client(port: UInt16, host: String)
    }
    enum Status {
        case None
        case Server(listen: DispatchSourceRead)
        case Client(listen: DispatchSourceRead, source: DispatchSourceTimer)
    }
    let master: CMTimebase
    let adjust: CMTimebase
    let object: UnsafeRawPointer
    let tick: @convention(c) (UnsafeRawPointer, UnsafePointer<CChar>, Int64) -> Void
    let info: @convention(c) (UnsafeRawPointer, CMTime) -> Void
    var status: Status
    var update: CMTime
    var timers: Dictionary<String, (CMTime, DispatchSourceTimer)>
    init(object maxobj: UnsafeRawPointer, outlet: (@convention(c) (UnsafeRawPointer, UnsafePointer<CChar>, Int64) -> Void, @convention(c)(UnsafeRawPointer, CMTime) -> Void)) throws {
        master = try.create(sourceClock: Self.clock)
        adjust = try.create(sourceTimebase: master)
        try master.setRate(1)
        try adjust.setRate(1)
        tick = outlet.0
        info = outlet.1
        status = .None
        object = maxobj
        update = adjust.time
        timers = [:]
    }
    deinit {
        removeAll()
        purge()
    }
}
extension Core {
    private func purge() {
        switch status {
        case .None:
            break
        case.Server(let listen):
            listen.cancel()
        case .Client(let listen, let source):
            source.cancel()
            listen.cancel()
        }
        status = .None
        update = master.time
    }
}
extension Core {
    var rate: Float64 {
        get {
            adjust.rate
        }
        set {
            do {
                try adjust.setRate(newValue)
                update = master.time
            } catch {
                Self.error(object, "set rate error due to \(error)")
            }
        }
    }
    var time: CMTime {
        get {
            adjust.time
        }
        set {
            do {
                try adjust.setTime(newValue)
                update = master.time
                scheduleAll(from: newValue)
            } catch {
                Self.error(object, "set rate error due to \(error)")
            }
        }
    }
}
extension Core {
    func sync(mode: Mode) {
        switch mode {
        case.None:
            purge()
        case.Server(let port):
            purge()
            let fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)
            guard 2 < fd else { return }
            let bounds = withUnsafePointer(to: sockaddr_in(sin_len: .init(MemoryLayout<sockaddr_in>.size),
                                                           sin_family: .init(AF_INET),
                                                           sin_port: .init(bigEndian: port),
                                                           sin_addr: .init(s_addr: 0),
                                                           sin_zero: (0, 0, 0, 0, 0, 0, 0, 0))) {
                bindresvport(fd, .init(mutating: $0))
            }
            guard bounds == 0 else {
                close(fd)
                return Self.error(object, "Bound error for port \(port)")
            }
            let handle = DispatchSource.makeReadSource(fileDescriptor: fd, queue: Self.queue)
            handle.setEventHandler {[weak self]in
                guard let self else { return }
                let buffer = [
                    .invalid,
                    .invalid,
                    adjust.time,
                    update
                ] as Array<CMTime>
                withUnsafeTemporaryAllocation(byteCount: MemoryLayout<sockaddr_in>.size, alignment: MemoryLayout<uintptr_t>.size) {
                    var socklen = socklen_t($0.count)
                    let sockref = $0.baseAddress?.assumingMemoryBound(to: sockaddr.self)
                    let income = 2 * MemoryLayout<CMTime>.stride
                    let recept = buffer.withUnsafeBytes {
                        recvfrom(fd, .init(mutating: $0.baseAddress), income, 0, sockref, &socklen)
                    }
                    guard income == recept else { return }
                    let outgoing = 4 * MemoryLayout<CMTime>.stride
                    let sent = sendto(fd, buffer, outgoing, 0, sockref, socklen)
                    guard outgoing == sent else { return }
                }
            }
            handle.setCancelHandler {[weak self]in
                close(fd)
            }
            handle.activate()
            status = .Server(listen: handle)
            break
        case.Client(let port, let host):
            purge()
            let fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)
            guard 2 < fd else { return }
            var s_addr = in_addr_t()
            let success = withUnsafeMutablePointer(to: &s_addr) {
                inet_pton(AF_INET, host, $0) == 1
            }
            guard success else {
                Self.error(object, "Invalid hostname")
                return
            }
            let target = sockaddr_in(sin_len: .init(MemoryLayout<sockaddr_in>.size),
                                     sin_family: .init(AF_INET),
                                     sin_port: .init(bigEndian: port),
                                     sin_addr: .init(s_addr: s_addr),
                                     sin_zero: (0, 0, 0, 0, 0, 0, 0, 0))
            let source = DispatchSource.makeTimerSource(flags: .strict, queue: Self.queue)
            source.setRegistrationHandler {[weak self]in
                guard let self else { return }
                try?master.addTimer(source)
                try?master.setTimerNextFireTime(source, fireTime: CMTimeAdd(master.time, CMTime(value: 1, timescale: 1)))
            }
            source.setEventHandler {[weak self]in
                guard let self else { return }
                let buffer = [
                    adjust.time,
                    master.time
                ] as Array<CMTime>
                let outgoing = 2 * MemoryLayout<CMTime>.stride
                let sent = withUnsafeBytes(of: target) {
                    sendto(fd, buffer, outgoing, 0, $0.baseAddress?.assumingMemoryBound(to: sockaddr.self), .init(MemoryLayout<sockaddr_in>.size))
                }
                try?master.setTimerNextFireTime(source, fireTime: CMTimeAdd(master.time, CMTime(value: 1, timescale: 1)))
                guard outgoing == sent else { return }
            }
            source.setCancelHandler {[weak self]in
                guard let self else { return }
                try?master.removeTimer(source)
            }
            let handle = DispatchSource.makeReadSource(fileDescriptor: fd, queue: Self.queue)
            var anchor = (CMTime.invalid, CMTime.invalid)
            var server = CMTime.indefinite
            var elapse = CMTime.positiveInfinity
            handle.setEventHandler {[weak self]in
                guard let self else { return }
                var buffer = [
                    .invalid,
                    .invalid,
                    .invalid,
                    .invalid,
                    adjust.time,
                    master.time,
                ] as Array<CMTime>
                let income = 4 * MemoryLayout<CMTime>.stride
                let recept = withUnsafeTemporaryAllocation(byteCount: MemoryLayout<sockaddr_in>.size, alignment: MemoryLayout<uintptr_t>.size) {
                    var socklen = socklen_t($0.count)
                    let sockref = $0.baseAddress?.assumingMemoryBound(to: sockaddr.self)
                    return recvfrom(fd, &buffer, income, 0, sockref, &socklen)
                }
                guard income == recept else { return }
                let peer = buffer[2]
                let sign = buffer[3]
                let this = CMTimeMultiplyByRatio(CMTimeAdd(buffer[4], buffer[0]), multiplier: 1, divisor: 2)
                let host = CMTimeMultiplyByRatio(CMTimeAdd(buffer[5], buffer[1]), multiplier: 1, divisor: 2)
                let lags = CMTimeSubtract(buffer[5], buffer[1])
				defer {
					info(object, CMTimeSubtract(peer, this))
				}
                if sign != server {
                    elapse = .positiveInfinity
                    server = sign
                    try?adjust.setRateAndAnchorTime(rate: 1, anchorTime: peer, referenceTime: host)
                    scheduleAll(from: peer)
                } else if lags < elapse {
                    elapse = lags
                    anchor = (peer, host)
                    try?adjust.setRateAndAnchorTime(rate: 1, anchorTime: peer, referenceTime: host)
                    scheduleAll(from: peer)
                } else if lags < CMTimeAbsoluteValue(CMTimeMultiplyByRatio(CMTimeSubtract(peer, this), multiplier: 1, divisor: 2)) {
                    let Δpeer = CMTimeSubtract(peer, anchor.0)
                    let Δhost = CMTimeSubtract(host, anchor.1)
                    try?adjust.setRateAndAnchorTime(rate: Δpeer.seconds / Δhost.seconds, anchorTime: peer, referenceTime: host)
                    scheduleAll(from: peer)
                }
            }
            handle.setCancelHandler {[weak self]in
                close(fd)
            }
            handle.activate()
            source.activate()
            status = .Client(listen: handle, source: source)
            break
        }
    }
}
extension Core {
    @inline(__always)
    private func countAndNext(time: CMTime, interval: CMTime) -> (CMTimeValue, CMTime) {
        let count = CMTimeValue(adjust.time.seconds/interval.seconds)
        return (count, CMTime(value: (count + 1) * interval.value, timescale: interval.timescale))
    }
    private func scheduleAll(from now: CMTime) {
        for (interval, timer) in timers.values {
            try?adjust.setTimerNextFireTime(timer, fireTime: countAndNext(time: now, interval: interval).1)
        }
    }
    private func removeAll() {
        for key in timers.keys {
            timers.removeValue(forKey: key)?.1.cancel()
        }
    }
    func tick(label: String, interval: CMTime) {
        timers.removeValue(forKey: label)?.1.cancel()
        if interval.isValid, interval.isNumeric, .zero < interval {
            let timer = DispatchSource.makeTimerSource(flags: .strict, queue: Self.queue)
            timer.setRegistrationHandler {[weak self]in
                guard let self else { return }
                try?adjust.addTimer(timer)
                try?adjust.setTimerToFireImmediately(timer)
            }
            timer.setEventHandler {[weak self]in
                guard let self else { return }
                let (count, next) = countAndNext(time: adjust.time, interval: interval)
                tick(object, label, count)
                try?adjust.setTimerNextFireTime(timer, fireTime: next)
            }
            timer.setCancelHandler {[weak self]in
                guard let self else { return }
                try?adjust.removeTimer(timer)
            }
            timer.activate()
            timers.updateValue((interval, timer), forKey: label)?.1.cancel()
        } else {
            timers.removeValue(forKey: label)?.1.cancel()
        }
    }
}
extension Core {
	static var version: String {
		Bundle(for: self).object(forInfoDictionaryKey: "CFBundleVersion")as?String ?? ""
	}
}
@_cdecl("core_new")
func new(object: UnsafeRawPointer, tick: UnsafeRawPointer, info: UnsafeRawPointer) -> UnsafeMutableRawPointer? {
    try?Unmanaged<Core>.passRetained(Core(object: object, outlet: (
        unsafeBitCast(tick, to: (@convention(c)(UnsafeRawPointer, UnsafePointer<CChar>, Int64) -> Void).self),
        unsafeBitCast(info, to: (@convention(c)(UnsafeRawPointer, CMTime) -> Void).self)
    ))).toOpaque()
}
@_cdecl("core_free")
func free(object: UnsafeMutableRawPointer) {
    Unmanaged<Core>.fromOpaque(object).release()
}
@_cdecl("core_gettime")
func bang(object: UnsafeMutableRawPointer) -> CMTime {
    Unmanaged<Core>.fromOpaque(object).takeUnretainedValue().time
}
@_cdecl("core_settime")
func setTime(object: UnsafeMutableRawPointer, value: CMTime) {
    Unmanaged<Core>.fromOpaque(object).takeUnretainedValue().time = value
}
@_cdecl("core_getrate")
func bang(object: UnsafeMutableRawPointer) -> Float64 {
    Unmanaged<Core>.fromOpaque(object).takeUnretainedValue().rate
}
@_cdecl("core_setrate")
func setTime(object: UnsafeMutableRawPointer, value: Float64) {
    Unmanaged<Core>.fromOpaque(object).takeUnretainedValue().rate = value
}
@_cdecl("core_single")
func`single`(object: UnsafeMutableRawPointer) {
    Unmanaged<Core>.fromOpaque(object).takeUnretainedValue().sync(mode: .None)
}
@_cdecl("core_server")
func`import`(object: UnsafeMutableRawPointer, port: UInt16) {
    Unmanaged<Core>.fromOpaque(object).takeUnretainedValue().sync(mode: .Server(port: port))
}
@_cdecl("core_client")
func`export`(object: UnsafeMutableRawPointer, port: UInt16, host: UnsafePointer<CChar>) {
    Unmanaged<Core>.fromOpaque(object).takeUnretainedValue().sync(mode: .Client(port: port, host: .init(cString: host)))
}
@_cdecl("core_tick")
func tick(object: UnsafeMutableRawPointer, label: UnsafePointer<CChar>, interval: CMTime) {
    Unmanaged<Core>.fromOpaque(object).takeUnretainedValue().tick(label: .init(cString: label), interval: interval)
}
@_cdecl("core_version_length")
func version() -> size_t {
	Core.version.count + 1
}
@_cdecl("core_version_string")
func version(target: UnsafeMutablePointer<Int8>) {
	target.initialize(from: Core.version, count: Core.version.count + 1)
}
