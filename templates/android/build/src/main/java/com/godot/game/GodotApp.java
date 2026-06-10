/**************************************************************************/
/*  GodotApp.java                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

package com.godot.game;

import org.godotengine.godot.Godot;
import org.godotengine.godot.GodotActivity;

import android.os.Bundle;
import android.util.Log;

import androidx.activity.EdgeToEdge;
import androidx.core.splashscreen.SplashScreen;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Template activity for Godot Android builds.
 * Feel free to extend and modify this class for your custom logic.
 */
public class GodotApp extends GodotActivity {
	private static final String TAG = "IDTXFlow";
	// Platform-specific USD plugin registry (ar, sdf, usd, usdGeom, etc.)
	private static final String USD_ANDROID_ASSET_PATH = "addons/IDTXFlow/bin/android/usd";
	// Cross-platform USD plugin data (godot integration, usdShaders, etc.)
	private static final String USD_PLUGIN_ASSET_PATH = "addons/IDTXFlow/bin/plugin/usd";

	static {
		// .NET libraries.
		if (BuildConfig.FLAVOR.equals("mono")) {
			try {
				Log.v("GODOT", "Loading System.Security.Cryptography.Native.Android library");
				System.loadLibrary("System.Security.Cryptography.Native.Android");
			} catch (UnsatisfiedLinkError e) {
				Log.e("GODOT", "Unable to load System.Security.Cryptography.Native.Android library");
			}
		}
	}

	private final Runnable updateWindowAppearance = () -> {
		Godot godot = getGodot();
		if (godot != null) {
			godot.enableImmersiveMode(godot.isInImmersiveMode(), true);
			godot.enableEdgeToEdge(godot.isInEdgeToEdgeMode(), true);
			godot.setSystemBarsAppearance();
		}
	};

	@Override
	public void onCreate(Bundle savedInstanceState) {
		// Extract USD plugin data BEFORE Godot initializes and loads GDExtension
		// libraries. The libusd_ms.so runtime needs filesystem access to plugInfo.json
		// files which are shipped as Android assets but must be on the real filesystem.
		extractUsdPluginData();

		SplashScreen.installSplashScreen(this);
		EdgeToEdge.enable(this);
		super.onCreate(savedInstanceState);
	}

	@Override
	public void onResume() {
		super.onResume();
		updateWindowAppearance.run();
	}

	@Override
	public void onGodotMainLoopStarted() {
		super.onGodotMainLoopStarted();
		runOnUiThread(updateWindowAppearance);
	}

	@Override
	public void onGodotForceQuit(Godot instance) {
		if (!BuildConfig.FLAVOR.equals("instrumented")) {
			// For instrumented builds, we disable force-quitting to allow the instrumented tests to complete
			// successfully, otherwise they fail when the process crashes.
			super.onGodotForceQuit(instance);
		}
	}

	/**
	 * Extracts ALL USD plugin data (plugInfo.json files, shaders, etc.) from
	 * Android assets to the app's internal storage and sets the
	 * PXR_PLUGINPATH_NAME environment variable so the USD runtime can locate
	 * its plugins via standard file I/O.
	 *
	 * Two asset trees are extracted:
	 *   bin/android/usd  → platform-specific plugin registry
	 *   bin/plugin/usd   → cross-platform Godot integration & shaders
	 */
	private void extractUsdPluginData() {
		try {
			File usdAndroidDir = new File(getFilesDir(), "usd/android");
			File usdPluginDir  = new File(getFilesDir(), "usd/plugin");

			// Always re-extract to ensure data is up-to-date after app updates
			copyPluginDirectory(USD_ANDROID_ASSET_PATH, usdAndroidDir);
			Log.v(TAG, "Extracted USD android data to: " + usdAndroidDir.getAbsolutePath());

			copyPluginDirectory(USD_PLUGIN_ASSET_PATH, usdPluginDir);
			Log.v(TAG, "Extracted USD plugin data to: " + usdPluginDir.getAbsolutePath());

			// Set the USD plugin search path — colon-separated list on Android (Unix)
			String pluginPath = usdAndroidDir.getAbsolutePath() + ":" + usdPluginDir.getAbsolutePath();
			android.system.Os.setenv("PXR_PLUGINPATH_NAME", pluginPath, true);
			Log.v(TAG, "Set PXR_PLUGINPATH_NAME to: " + pluginPath);
		} catch (Exception e) {
			Log.e(TAG, "Failed to extract USD plugin data: " + e.getMessage(), e);
		}
	}

	/**
	 * Recursively copies an asset directory to a target filesystem directory.
	 */
	private void copyPluginDirectory(String assetPath, File targetDir) throws IOException {
		String[] children = getAssets().list(assetPath);
		if (children == null || children.length == 0) {
			// Leaf node — could be a file or an empty directory; try as file
			copyAssetFile(assetPath, targetDir);
			return;
		}
		// It is a directory — recurse into children
		targetDir.mkdirs();
		for (String child : children) {
			copyPluginDirectory(assetPath + "/" + child, new File(targetDir, child));
		}
	}

	/**
	 * Copies a single asset file to a target filesystem path.
	 */
	private void copyAssetFile(String assetPath, File targetFile) throws IOException {
		targetFile.getParentFile().mkdirs();
		try (InputStream in = getAssets().open(assetPath);
		     OutputStream out = new FileOutputStream(targetFile)) {
			byte[] buffer = new byte[4096];
			int bytesRead;
			while ((bytesRead = in.read(buffer)) != -1) {
				out.write(buffer, 0, bytesRead);
			}
		}
	}
}
