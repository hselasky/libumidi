import android.media.midi.*;
import android.os.*;

public class umidi20_android$open implements MidiManager.OnDeviceOpenedListener {
        public native void onDeviceOpened(MidiDevice dev);

	public void openDevice(MidiManager m, MidiDeviceInfo info) {
		m.openDevice(info, this,
                    new Handler(Looper.getMainLooper()));
	}
}
