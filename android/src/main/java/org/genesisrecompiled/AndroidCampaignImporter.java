package org.genesisrecompiled;

import android.content.Context;
import com.chaquo.python.PyObject;
import com.chaquo.python.Python;
import com.chaquo.python.android.AndroidPlatform;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

public final class AndroidCampaignImporter {
    private static File recipe;

    private AndroidCampaignImporter() {}

    private static void stage(Context context, String asset, File destination) throws IOException {
        String[] children = context.getAssets().list(asset);
        if (children == null) throw new IOException("Import recipe is missing from the application.");
        if (children.length > 0) {
            if (!destination.isDirectory() && !destination.mkdir())
                throw new IOException("Cannot prepare application storage for the import recipe.");
            for (String child : children) stage(context, asset + "/" + child, new File(destination, child));
            return;
        }
        try (InputStream input = context.getAssets().open(asset);
             FileOutputStream output = new FileOutputStream(destination)) {
            byte[] chunk = new byte[64 * 1024];
            for (int count; (count = input.read(chunk)) != -1;) output.write(chunk, 0, count);
        }
    }

    /** Called from the native Mods import worker. No converter proc or shell. */
    public static synchronized byte[] run(Context context, byte[][] arguments) {
        try {
            if (recipe == null) {
                File candidate = new File(context.getCodeCacheDir(), "campaign-converter").getCanonicalFile();
                stage(context, "campaign-converter", candidate);
                if (!Python.isStarted()) Python.start(new AndroidPlatform(context.getApplicationContext()));
                Python.getInstance().getModule("sys").get("path").callAttr("insert", 0, candidate.getPath());
                recipe = candidate;
            }
            String[] argv = new String[arguments.length];
            for (int i = 0; i < arguments.length; ++i)
                argv[i] = new String(arguments[i], StandardCharsets.UTF_8);
            PyObject detail = Python.getInstance().getModule("android_campaign_import").callAttr("run", (Object)argv);
            return detail == null ? null : detail.toString().getBytes(StandardCharsets.UTF_8);
        } catch (Exception failure) {
            android.util.Log.e("GenesisCampaignImport", "Converter failed", failure);
            return "The game could not run the installed import converter.".getBytes(StandardCharsets.UTF_8);
        }
    }
}