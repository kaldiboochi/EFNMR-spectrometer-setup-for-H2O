import serial
import struct
import sys
import threading
import queue
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as animation

# --- Configuration ---
SERIAL_PORT = '/dev/ttyACM0'
BAUD_RATE = 115200
SAMPLE_RATE = 10000  # 10kSPS as set on the Pico

# A thread-safe queue to pass data from the serial thread to the main thread
data_queue = queue.Queue()

def serial_worker(pulse_ms, read_ms, stop_event):
    """Runs in a separate thread to handle serial communication."""
    try:
        print(f"Connecting to Pico on {SERIAL_PORT}...")
        with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2) as ser:
            ser.flushInput()
            initial_message = ser.readline().decode('utf-8').strip()
            print(f"Pico says: '{initial_message}'")
            
            if "" not in initial_message:
                print("Error: Did not receive ready message from Pico.")
                return

            # Send the NMR command
            command = f"NMR({pulse_ms},{read_ms})\n"
            print(f"--> Sending command to Pico: {command.strip()}")
            ser.write(command.encode('utf-8'))
            
            # Read status messages until the data stream starts
            while not stop_event.is_set():
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(line)
                    if "Starting data read" in line:
                        break
            
            # Now, read the binary data stream
            samples_to_read = SAMPLE_RATE * (read_ms / 1000.0)
            samples_read = 0
            while not stop_event.is_set() and samples_read < samples_to_read:
                raw_bytes = ser.read(2)
                if len(raw_bytes) == 2:
                    sample, = struct.unpack('<H', raw_bytes)
                    value = sample & 0x0FFF # Mask out the sync bit
                    data_queue.put(value) # Put the clean value into the queue
                    samples_read += 1
                else:
                    break
            print("--- Serial worker finished. ---")

    except serial.SerialException as e:
        print(f"SERIAL ERROR: {e}")
    finally:
        stop_event.set() # Signal the main thread to stop

def update_plot(frame, all_samples, line_time, line_fft, ax_fft):
    """This function is called periodically to update the plot."""
    new_data = []
    while not data_queue.empty():
        new_data.append(data_queue.get())
    
    if not new_data:
        return line_time, line_fft

    all_samples.extend(new_data)
    
    # --- Update Time Domain Plot (Top) ---
    line_time.set_ydata(all_samples)
    line_time.set_xdata(np.arange(len(all_samples)))
    
    # --- Update Frequency Domain Plot (Bottom) ---
    if len(all_samples) > 1:
        # Perform FFT
        fft_result = np.fft.fft(all_samples)
        fft_magnitude = np.abs(fft_result)
        
        # Generate frequency axis
        fft_freq = np.fft.fftfreq(len(all_samples), 1.0 / SAMPLE_RATE)
        
        # We only care about the positive frequencies
        positive_freq_indices = np.where(fft_freq >= 0)
        
        line_fft.set_xdata(fft_freq[positive_freq_indices])
        line_fft.set_ydata(fft_magnitude[positive_freq_indices])
        ax_fft.relim()
        ax_fft.autoscale_view()

    # Rescale the time domain plot
    ax_time = line_time.axes
    ax_time.relim()
    ax_time.autoscale_view()
    
    return line_time, line_fft

def main():
    if len(sys.argv) != 3:
        print("Usage: python plot_nmr.py <pulse_width_ms> <read_time_ms>")
        sys.exit(1)
    pulse_ms, read_ms = map(int, sys.argv[1:])

    # --- Setup the plot ---
    fig, (ax_time, ax_fft) = plt.subplots(2, 1)
    fig.suptitle('NMR Signal Analysis')
    
    # Top plot: Time Domain
    ax_time.set_title('Time Domain Signal (FID)')
    ax_time.set_xlabel('Sample Number')
    ax_time.set_ylabel('ADC Value (0-4095)')
    ax_time.set_ylim(0, 4096)
    line_time, = ax_time.plot([], [])
    
    # Bottom plot: Frequency Domain
    ax_fft.set_title('Frequency Domain (FFT)')
    ax_fft.set_xlabel('Frequency (Hz)')
    ax_fft.set_ylabel('Magnitude')
    ax_fft.set_xlim(0, SAMPLE_RATE / 2) # Only show positive frequencies up to Nyquist
    ax_fft.set_yscale('log')
    line_fft, = ax_fft.plot([], [])
    
    plt.tight_layout(rect=[0, 0, 1, 0.96])
    
    all_samples = []
    stop_event = threading.Event()

    # --- Start the serial worker thread ---
    worker = threading.Thread(target=serial_worker, args=(pulse_ms, read_ms, stop_event))
    worker.start()

    # --- Start the plot animation ---
    ani = animation.FuncAnimation(fig, update_plot, fargs=(all_samples, line_time, line_fft, ax_fft),
                                  interval=100, blit=True)
    
    plt.show() # This blocks until the plot window is closed

    # --- Cleanup ---
    print("Plot window closed. Stopping worker thread...")
    stop_event.set()
    worker.join()
    print("Program finished.")

if __name__ == '__main__':
    main()
