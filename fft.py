import librosa
import librosa.display
import matplotlib.pyplot as plt
import numpy as np
import os

def get_smoothed_spectrum(file_path):
    """
    Calculates the smoothed frequency spectrum for a given audio file.

    Args:
        file_path (str): The path to the audio file.

    Returns:
        A tuple of (frequencies, mean_spectrum_db) if successful,
        otherwise (None, None).
    """
    try:
        # Load the audio file, preserving its original sample rate
        y, sr = librosa.load(file_path, sr=None)
        print(f"Successfully loaded '{os.path.basename(file_path)}' (SR: {sr} Hz)")

        # Compute the Short-Time Fourier Transform (STFT)
        D = librosa.stft(y)

        # Convert the complex-valued STFT output to a dB-scaled magnitude
        S_db = librosa.amplitude_to_db(np.abs(D), ref=np.max)

        # Average the spectrum across all time frames to get a single smoothed line
        mean_spectrum_db = np.mean(S_db, axis=1)

        # Get the frequency values corresponding to the FFT bins
        frequencies = librosa.fft_frequencies(sr=sr)
        
        return frequencies, mean_spectrum_db

    except Exception as e:
        print(f"Error processing {file_path}: {e}")
        return None, None

# --- Main execution ---
if __name__ == '__main__':
    # --- IMPORTANT: Set your file paths here ---
    file1_path = '1.aac'
    file2_path = '2.aac'

    # Get spectrum data for both files
    freq1, spec1 = get_smoothed_spectrum(file1_path)
    freq2, spec2 = get_smoothed_spectrum(file2_path)

    # Proceed to plot only if both files were processed successfully
    if freq1 is not None and freq2 is not None:
        # Create the plot
        plt.figure(figsize=(14, 7))

        # Plot the spectrum for the first file
        plt.plot(freq1, spec1, color='crimson', label=f'File 1: {os.path.basename(file1_path)}')

        # Plot the spectrum for the second file
        plt.plot(freq2, spec2, color='royalblue', label=f'File 2: {os.path.basename(file2_path)}')
        
        # --- Chart Styling ---
        # The x-axis scale is now linear by default (plt.xscale('log') is removed)
        plt.title('Comparative Audio Frequency Spectrum (1000-2000 Hz)')
        plt.xlabel('Frequency (Hz) [Linear Scale]')
        plt.ylabel('Magnitude (dB)')
        
        # Set the x-axis limits to the desired range: 1000 Hz to 2000 Hz
        plt.xlim([1000, 3000])
        
        plt.grid(True, which="both", ls="--", alpha=0.6)
        plt.legend() # Display the legend to identify the lines
        plt.show()
