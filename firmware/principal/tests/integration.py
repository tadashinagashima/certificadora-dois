"""Integration through the production text protocol, on native executable or ESP32."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import queue
import re
import subprocess
import threading
import time


class Client:
    def __init__(self, args):
        self.args = args
        self.log = []
        self.pgndata = {}
        self.metrics = []
        self.process = None
        self.serial = None
        self.commands = 0
        self.boot = []
        if args.exe:
            self.process = subprocess.Popen([args.exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            self.lines = queue.Queue()
            def reader():
                for line in iter(self.process.stdout.readline, b''):
                    self.lines.put(line)
                self.lines.put(b'')
            threading.Thread(target=reader, daemon=True).start()
        else:
            import serial
            self.serial = serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=5)
            self.serial.dtr = False
            self.serial.rts = False
            self.serial.port = args.port
            self.serial.open()
            self.serial.reset_input_buffer()
            self.serial.rts = True
            time.sleep(0.1)
            self.serial.rts = False
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                line = self.read(deadline)
                self.boot.append(line)
                if line == 'READY protocol=1':
                    break
            assert 'READY protocol=1' in self.boot, self.boot
            assert 'PLATFORM arduino=3.3.12 idf=v5.5.5' in self.boot
            expected = hashlib.sha256(Path(args.elf).read_bytes()).hexdigest()
            assert 'ELF_SHA256 ' + expected in self.boot
            assert any('cpu_mhz=240 flash_bytes=4194304 psram_bytes=0' in x for x in self.boot)

    def read(self, deadline):
        while time.monotonic() < deadline:
            if self.process:
                raw = self.lines.get(timeout=max(0.01, deadline-time.monotonic()))
                if not raw:
                    raise RuntimeError('Native process exited')
            else:
                raw = self.serial.readline()
                if not raw:
                    continue
            line = raw.decode('utf-8', errors='replace').rstrip('\r\n')
            self.log.append('RX ' + line)
            assert not re.search(r'Guru Meditation|panic|abort\(\)|Stack canary|watchdog.*triggered', line, re.I), line
            return line
        raise TimeoutError('No response before deadline')

    def write(self, raw):
        self.log.append('TX ' + repr(raw))
        stream = self.process.stdin if self.process else self.serial
        stream.write(raw)
        stream.flush()

    def response(self):
        lines = []
        deadline = time.monotonic()+15
        while True:
            line = self.read(deadline)
            assert not line.startswith('BOOT '), 'Unexpected reset'
            if line == 'DONE':
                return lines
            lines.append(line)

    def cmd(self, command, expected=None):
        self.write((command+'\n').encode())
        self.commands += 1
        result = self.response()
        if expected is not None:
            assert result == [expected], (command, expected, result)
        return result

    def state(self):
        lines = self.cmd('status')
        info = dict(re.findall(r'(\w+)=([^ ]+)', lines[0]))
        info['state'] = lines[0].split()[1]
        info['fen'] = lines[1][4:]
        info.update(dict(re.findall(r'(\w+)=([^ ]+)', lines[2])))
        info['ending'] = lines[3]
        info['claims'] = lines[4]
        return info

    def new(self, scenario='start'):
        self.cmd('new '+scenario, 'OK new '+scenario)
        self.cmd('sync', 'OK observe')

    def play(self, source, target, captured=None, promotion=None, verify=True):
        before = self.state() if verify else None
        self.cmd('lift '+source, 'OK observe')
        if captured:
            self.cmd('lift '+captured, 'OK observe')
        self.cmd('place '+target, 'OK observe')
        if verify:
            pending = self.state()
            assert pending['fen'] == before['fen'] and pending['plies'] == before['plies']
        if promotion:
            self.cmd('confirm', 'CONFIRM PromotionRequired')
            self.cmd('promote '+promotion, 'OK promote')
        self.cmd('confirm', 'CONFIRM Accepted')
        if verify:
            after = self.state()
            assert int(after['plies']) == int(before['plies'])+1
            assert after['fault'] == 'Ok'

    def pgn(self, label=None):
        lines = self.cmd('pgn')
        assert lines[0] == 'PGN_BEGIN' and lines[-1] == 'PGN_END', lines
        pgn = '\n'.join(lines[1:-1])+'\n'
        if label:
            self.pgndata[label] = pgn
        return pgn

    def check_pgn(self, body, result='*', label=None, custom=False):
        pgn = self.pgn(label)
        tags, text = pgn.split('\n\n',1)
        expected = '\n'.join(f'[{key} "{value}"]' for key,value in
            [('Event','?'),('Site','?'),('Date','????.??.??'),('Round','?'),('White','?'),('Black','?'),('Result',result)])
        if custom:
            assert tags.startswith(expected+'\n[FEN "') and tags.endswith('[SetUp "1"]'), tags
        else:
            assert tags == expected, tags
        assert ' '.join(text.split()) == ((body+' ') if body else '')+result, (body, text)
        assert text.endswith('\n\n')
        assert all(len(line) <= 79 for line in text.splitlines())
        return pgn

    def metric(self, label):
        if self.process:
            return None
        lines = self.cmd('metrics')
        assert len(lines) == 1 and lines[0].startswith('METRICS '), lines
        item = {key:int(value) for key,value in re.findall(r'(\w+)=(\d+)',lines[0])}
        item['label'] = label
        assert item['heap_ok'] == 1 and item['watchdog'] == 1
        assert item['stack_free_bytes'] >= 2048
        assert item['heap_min_bytes'] > 32768
        self.metrics.append(item)
        return item

    def close(self):
        if self.process:
            self.process.stdin.close()
            self.process.wait(timeout=5)
        if self.serial:
            self.serial.close()


def lifecycle(c):
    c.cmd('new', 'OK new start')
    s = c.state()
    assert s['state'] == 'AwaitingPosition' and s['plies'] == '0'
    c.check_pgn('')
    c.cmd('lift e2', 'ERROR NoObservation')
    c.cmd('confirm', 'CONFIRM UnreliableInput')
    c.cmd('observe ffff00000000ffff', 'OK observe')
    c.cmd('confirm', 'CONFIRM NoMove')
    c.play('e2','e4')
    c.cmd('confirm','CONFIRM NoMove')
    c.check_pgn('1. e4')
    c.cmd('finish','OK finish')
    c.check_pgn('1. e4',label='manual')
    assert c.state()['record_finished'] == '1'
    for command, reply in [('confirm','CONFIRM GameFinished'),('finish','ERROR GameFinished'),('sync','ERROR GameFinished')]:
        c.cmd(command,reply)
    for color,result in [('white','0-1'),('black','1-0')]:
        c.new()
        c.cmd('lift e2','OK observe')
        c.cmd('resign '+color,'OK finish')
        c.check_pgn('', result, 'resign_'+color)
    c.new()
    assert c.state()['plies'] == '0'
    for scenario,result,reason in [('mate','1-0','Checkmate'),('stalemate','1/2-1/2','Stalemate'),('insufficient','1/2-1/2','InsufficientMaterial')]:
        c.cmd('new '+scenario,'OK new '+scenario)
        s=c.state()
        assert s['state']=='Finished' and s['record_finished']=='1' and reason in s['ending']
        c.check_pgn('',result,scenario,custom=True)


def games(c):
    c.new()
    for source,target in [('f2','f3'),('e7','e5'),('g2','g4'),('d8','h4')]:
        c.play(source,target)
    c.check_pgn('1. f3 e5 2. g4 Qh4#','0-1','fools_mate')
    s=c.state()
    assert s['state']=='Finished' and s['record_finished']=='1'
    assert s['fen']=='rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3'
    c.new()
    for source,target in [('e2','e4'),('e7','e5'),('f1','c4'),('b8','c6'),('d1','h5'),('g8','f6')]:
        c.play(source,target)
    c.play('h5','f7','f7')
    c.check_pgn('1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6 4. Qxf7#','1-0','scholars_mate')
    assert c.state()['record_finished']=='1'


def captures(c):
    c.new()
    c.play('e2','e4')
    c.play('d7','d5')
    before=c.state()['fen']
    c.cmd('lift e4','OK observe')
    c.cmd('confirm','CONFIRM NoLegalMatch')
    assert c.state()['fen']==before
    c.check_pgn('1. e4 d5')
    c.cmd('sync','OK observe')
    c.play('e4','d5','d5')
    c.play('d8','d5','d5')
    c.play('b1','c3')
    c.check_pgn('1. e4 d5 2. exd5 Qxd5 3. Nc3',label='captures')


def castling(c):
    for side,rank,prefix in [('w','1','1.'),('b','8','1...')]:
        for target,rook,rook_to,san in [('g','h','f','O-O'),('c','a','d','O-O-O')]:
            c.new('castle_'+side)
            c.cmd('lift e'+rank,'OK observe')
            c.cmd('place '+target+rank,'OK observe')
            c.cmd('lift '+rook+rank,'OK observe')
            c.cmd('place '+rook_to+rank,'OK observe')
            c.cmd('confirm','CONFIRM Accepted')
            c.check_pgn(prefix+' '+san,label='castle_'+side+target,custom=True)
            s=c.state()
            assert s['plies']=='1' and s['official']==s['observed']


def en_passant(c):
    for side,source,target,captured,prefix in [('w','e5','d6','d5','1.'),('b','e4','d3','d4','1...')]:
        c.new('ep_'+side)
        c.play(source,target,captured)
        c.check_pgn(prefix+' ex'+target,label='ep_'+side,custom=True)
        s=c.state()
        assert s['official']==s['observed']


def promotions(c):
    for side,source,rank,prefix in [('w','a7','8','1.'),('b','a2','1','1...')]:
        for capture in [False,True]:
            for piece in ['Q','R','B','N']:
                scenario=('promotion_capture_' if capture else 'promotion_')+side
                target=('b' if capture else 'a')+rank
                c.new(scenario)
                c.play(source,target,target if capture else None,piece)
                result='1/2-1/2' if piece in ['B','N'] else '*'
                san=('ax' if capture else '')+target+'='+piece+('+' if piece in ['Q','R'] else '')
                c.check_pgn(prefix+' '+san,result,scenario+piece,custom=True)
    c.new('promotion_w')
    c.cmd('lift a7','OK observe'); c.cmd('place a8','OK observe')
    before=c.state()['fen']
    c.cmd('confirm','CONFIRM PromotionRequired')
    c.cmd('confirm','CONFIRM PromotionRequired')
    c.cmd('promote K','ERROR Promotion')
    c.cmd('promote Q','OK promote')
    c.cmd('lift a8','OK observe'); c.cmd('place a8','OK observe')
    assert c.state()['promotion']=='None'
    c.cmd('confirm','CONFIRM PromotionRequired')
    c.cmd('promote N','OK promote')
    assert c.state()['fen']==before and c.state()['plies']=='0'
    c.cmd('finish','OK finish')
    c.check_pgn('',label='cancel_promotion',custom=True)


def recovery(c):
    c.new()
    initial=c.state()['fen']
    c.cmd('lift e2','OK observe'); c.cmd('place e5','OK observe')
    c.cmd('confirm','CONFIRM NoLegalMatch')
    c.cmd('confirm','CONFIRM PositionMismatch')
    assert c.state()['fen']==initial
    c.cmd('sync','OK observe')
    c.cmd('lift e2','OK observe'); c.cmd('interrupt','OK interrupt')
    c.cmd('place e4','ERROR NoObservation')
    c.cmd('confirm','CONFIRM UnreliableInput')
    c.cmd('sync','OK observe')
    for _ in range(33):
        c.cmd('lift b1','OK observe'); c.cmd('place b1','OK observe')
    assert c.state()['overflow']=='1'
    c.cmd('confirm','CONFIRM UnreliableInput')
    assert c.state()['fen']==initial
    c.cmd('sync','OK observe'); c.play('e2','e4')
    c.check_pgn('1. e4')
    c.new('ambiguous')
    initial=c.state()['fen']
    c.cmd('lift a8','OK observe'); c.cmd('lift b1','OK observe')
    c.cmd('place a8','OK observe'); c.cmd('place b1','OK observe')
    c.cmd('lift a1','OK observe'); c.cmd('confirm','CONFIRM Ambiguous')
    assert c.state()['fen']==initial and c.state()['plies']=='0'
    c.cmd('sync','OK observe')
    c.play('a1','a8','a8')
    c.check_pgn('1. Rxa8+',label='ambiguity_recovered',custom=True)


def protocol(c):
    c.new(); c.play('e2','e4')
    original=c.pgn()
    before=c.state()['fen']
    for command in ['', 'confirm extra','observe 1','observe -fffffffffffffff','observe 10000000000000000','observe fffffffffffffffz','lift i2','place a0','sync x','resign green','new unknown','pgn extra',' confirm','observe  ffff00000000ffff']:
        reply=c.cmd(command)
        assert reply[0].startswith('ERROR '), (command,reply)
        assert c.state()['fen']==before and c.pgn()==original
    for raw in [b'x'*97+b'confirm\n',b'lift\x00 d7\n',b'con\rfirm\n',b'\xffconfirm\n']:
        c.write(raw)
        assert c.response()==['ERROR Line']
        assert c.state()['fen']==before
    c.write(b'status\r\n')
    assert c.response()[0].startswith('STATE ')
    # Fragmented lines and several complete lines in one write.
    c.write(b'sy'); c.write(b'nc\nconfirm\n')
    assert c.response()==['OK observe']
    assert c.response()==['CONFIRM NoMove']
    if c.serial:
        c.write(b'con')
        time.sleep(2.2)
        c.write(b'firm\n')
        assert c.response()==['ERROR Line']
        assert c.state()['fen']==before
    c.cmd('sync','OK observe'); c.play('e7','e5')
    c.check_pgn('1. e4 e5',label='protocol_recovered')


def capacity(c):
    c.new()
    steps=[('g1','f3','Nf3'),('g8','f6','Nf6'),('f3','g1','Ng1'),('f6','g8','Ng8')]
    body=''
    count=0
    while count<3000:
        source,target,san=steps[count%4]
        addition=(' ' if count else '')+(str(count//2+1)+'. ' if count%2==0 else '')+san
        if len(body)+len(addition)>8192:
            previous=c.state()
            pgn=c.pgn()
            c.cmd('lift '+source,'OK observe'); c.cmd('place '+target,'OK observe')
            c.cmd('confirm','ERROR RecordFailure CapacityExceeded')
            after=c.state()
            assert after['fault']=='CapacityExceeded'
            assert after['fen']==previous['fen'] and after['plies']==previous['plies']
            assert c.pgn('capacity_preserved')==pgn
            for blocked in ['confirm','sync','finish','resign white','promote Q','interrupt']:
                c.cmd(blocked,'ERROR FaultLatched CapacityExceeded')
            c.check_pgn(body)
            c.metric('capacity_fault')
            c.new(); c.play('e2','e4'); c.check_pgn('1. e4')
            return
        c.play(source,target,verify=False)
        body+=addition
        count+=1
        if count%250==0:
            print('capacity plies='+str(count),flush=True)
            c.metric('capacity_'+str(count))
    raise AssertionError('Capacity not reached')


def history(c):
    c.new()
    for _ in range(25):
        for source,target in [('g1','f3'),('g8','f6'),('f3','g1'),('f6','g8')]:
            c.play(source,target,verify=False)
    s=c.state()
    assert s['claims']=='CLAIMS repetition=1 fifty=1' and s['state']=='Tracking'
    c.cmd('finish','OK finish')
    assert c.state()['plies']=='100'
    c.pgn('repetition_100plies')
    c.new()
    assert c.state()['claims']=='CLAIMS repetition=0 fifty=0'
    c.new('busy')
    c.play('e2','a6','a6')
    c.check_pgn('1. Bxa6',label='busy',custom=True)


SUITES={f.__name__:f for f in [lifecycle,games,captures,castling,en_passant,promotions,recovery,protocol,capacity,history]}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    target=parser.add_mutually_exclusive_group(required=True)
    target.add_argument('--exe')
    target.add_argument('--port')
    parser.add_argument('--elf')
    parser.add_argument('--suite',choices=list(SUITES))
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    if args.port and not args.elf:
        parser.error('--elf is required with --port')
    if args.output:
        args.output.mkdir(parents=True,exist_ok=False)
    report={'startedAtUtc':datetime.now(timezone.utc).isoformat(),'target':args.exe or args.port,'success':False,'suites':[]}
    c=None
    try:
        c=Client(args)
        c.metric('boot')
        for name,run in SUITES.items():
            if args.suite and args.suite!=name:
                continue
            start=time.monotonic()
            run(c)
            report['suites'].append({'name':name,'passed':True,'seconds':round(time.monotonic()-start,3)})
            c.metric(name)
            print('PASS '+name,flush=True)
        report['success']=True
    except Exception as error:
        report['failure']=repr(error)
        print('FAIL '+repr(error),flush=True)
    finally:
        report['finishedAtUtc']=datetime.now(timezone.utc).isoformat()
        if c:
            report.update(commands=c.commands,metrics=c.metrics,boot=c.boot)
            if args.output:
                (args.output/'serial.log').write_text('\n'.join(c.log)+'\n',encoding='utf-8')
                for name,pgn in c.pgndata.items():
                    (args.output/(name+'.pgn')).write_text(pgn,encoding='utf-8')
            c.close()
        if args.output:
            (args.output/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return 0 if report['success'] else 1


if __name__=='__main__':
    raise SystemExit(main())
