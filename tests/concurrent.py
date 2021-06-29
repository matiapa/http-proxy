import threading
import time
import os
from random import randrange
from multiprocessing import Process, current_process

def thread_function(code):
    time.sleep(randrange(4))
    res = os.popen('curl -s  http://127.0.0.1:2020/un.jpg --proxy localhost:8080 | sha256sum').read().split(" ")[0]
    if (res != code):
        print("Failed")
    else:
        print("Success")
    exit()

if __name__ == '__main__':
    code = os.popen('curl -s  http://127.0.0.1:2020/un.jpg | sha256sum').read().split(" ")[0]
    print(code)
    for i in range(50):
        client = Process(name='Client', target=thread_function, args=(code, ))
        client.start()