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
    var status: Status
    var update: CMTime
    init(object maxobj: UnsafeRawPointer) throws {
        master = try.init(sourceClock: Self.clock)
        adjust = try.init(sourceTimebase: master)
        try master.setRate(1)
        try adjust.setRate(1)
        status = .None
        object = maxobj
        update = adjust.time
    }
    deinit {
        purge()
    }
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
    var rate: Float64 {
        get {
            adjust.rate
        }
        set {
            do {
                try adjust.setRate(newValue)
                purge()
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
                purge()
            } catch {
                Self.error(object, "set rate error due to \(error)")
            }
        }
    }
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
            handle.resume()
            status = .Server(listen: handle)
            break
        case.Client(let port, let host):
            purge()
            let fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)
            guard 2 < fd else { return }
            let s_addr = withUnsafeTemporaryAllocation(of: in_addr_t.self, capacity: 1) {
                inet_pton(AF_INET, host, $0.baseAddress)
                return $0[0]
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
                let τ = buffer[2]
                let t = CMTimeMultiplyByRatio(CMTimeAdd(buffer[5], buffer[1]), multiplier: 1, divisor: 2)
                let Δ = CMTimeSubtract(buffer[5], buffer[1])
                if server != buffer[3] {
                    elapse = .positiveInfinity
                    server = buffer[3]
                    try?adjust.setRateAndAnchorTime(rate: 1, anchorTime: τ, referenceTime: t)
                } else if Δ < elapse {
                    elapse = Δ
                    anchor = (τ, t)
                    try?adjust.setRateAndAnchorTime(rate: 1, anchorTime: τ, referenceTime: t)
                } else if Δ < CMTimeAbsoluteValue(CMTimeSubtract(τ, t)) {
                    let Δτ = CMTimeSubtract(τ, anchor.0)
                    let Δt = CMTimeSubtract(t, anchor.1)
                    try?adjust.setRateAndAnchorTime(rate: Δτ.seconds / Δt.seconds, anchorTime: τ, referenceTime: t)
                }
            }
            handle.setCancelHandler {[weak self]in
                close(fd)
            }
            handle.resume()
            source.resume()
            status = .Client(listen: handle, source: source)
            break
        }
    }
}
@_cdecl("core_new")
func new(object: UnsafeRawPointer) -> UnsafeMutableRawPointer? {
    try?Unmanaged<Core>.passRetained(Core(object: object)).toOpaque()
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
