import cv2
import sys
import time
from signal import signal, SIGINT
from threading import Thread, Lock
from pyzbar.pyzbar import decode
import json

mutex = Lock()
mutex2 = Lock()
frame_raw = []
frame_data = {}
cam = cv2.VideoCapture(0)

debug = False
dying = False

def ctrlc(signal_received, frame):
    print("bye!")
    dying = True
    sys.exit(0)

signal(SIGINT, ctrlc)

def startup():
    cam.set(cv2.CAP_PROP_FRAME_WIDTH, 800)
    cam.set(cv2.CAP_PROP_FRAME_HEIGHT,600)
    cam.set(cv2.CAP_PROP_BRIGHTNESS, 100)
    time.sleep(.2)    
#    _, img = cam.read()
#    img = cv2.flip(img, -1)
#    cv2.imwrite("frame.jpg", img) 


def cleanup():
    cam.release()
    cv2.destroyAllWindows()

def video_reader(thread_id):

    ctr = 0 
    t_end = time.time() + 100

    global dying    
    while time.time() < t_end and not dying:
        _, img = cam.read()
        if debug:
            ctr += 1
            if ctr % 50 == 0:
                print("writing frame.jpg")
                cv2.imwrite("frame.jpg", img)
        mutex.acquire()
        try:
            frame_raw.append(img)
        finally:
            mutex.release()

def video_decoder(thread_id):
    global dying    
    while not dying:
        mutex.acquire()
        img = None
        if len(frame_raw) > 0:
            img = frame_raw.pop(0)
            mutex.release()
        else:
            mutex.release()
            time.sleep(0.1)
            print("thread waiting", thread_id)
            continue
        img = cv2.flip(img, -1)
        dimg = decode(img)
        if len(dimg) > 0:
            b = dimg[0].data
            frame_count = int(b[6:8], 16)
            frame_number = int(b[4:6],16) - 1
            print(frame_number, frame_count)
            if frame_number in frame_data:
                continue

            mutex2.acquire()
            try:
                frame_data[frame_number] = b[8:]
                if len(frame_data) == frame_count:
                    full_data = b''
                    for i in range(frame_count):
                        full_data += frame_data[i]
                    print(full_data.decode('utf-8'))
                    dying = True
            finally:
                mutex2.release()

            
startup()
a = Thread(target=video_reader, args=("a"))
b = Thread(target=video_decoder, args=("b"))
c = Thread(target=video_decoder, args=("c"))
d = Thread(target=video_decoder, args=("d"))

a.start()
b.start()
c.start()
d.start()

b.join()
c.join()
d.join()

cleanup()





