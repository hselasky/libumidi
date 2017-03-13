import android.media.midi.*;
import android.os.*;

public class umidi20_android$send extends MidiReceiver {
        public native void onSend(byte[] msg, int a, int b, long c);
}
