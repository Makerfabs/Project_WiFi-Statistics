import numpy as np
import matplotlib.pyplot as plt
import math

#File which you want analysis
trace_file_name = "./log3.txt"

time_list = []
count_list = []
width = 0


def read_WiFi_data(filename):
    f = open(filename, 'r')
    while True:
        line = f.readline()
        if not line: 
            break
        

        if line.find('TIME') != -1:

            data = eval(line)
            time = data['TIME']
            count = data['COUNT']

            time_list.append(int(time))
            count_list.append(int(count))

    global width
    width = len(count_list)
    f.close()
    print("Read over")
    print(time_list)
    print(count_list)


def main():
    read_WiFi_data(trace_file_name)

    plt.plot(time_list, count_list)
    plt.title("WiFi Devs Count")
    plt.xlabel("Time")
    plt.ylabel("Count")    
    plt.draw()
    plt.pause(0)
    pass


if __name__ == "__main__":
    main()
