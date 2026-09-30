package app.pvg3;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.webkit.JavascriptInterface;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Collections;

/** Full-screen in-game editor. Its files are bundled in the APK; no browser
 * app, network request, login or account is needed to edit or autosave levels. */
public final class MakerActivity extends Activity {
    private static final String ORIGIN = "pvg3.local";
    private static final int REQUEST_OPEN_TXT = 7101;
    private static final int REQUEST_SAVE_TXT = 7102;

    private WebView webView;
    private ValueCallback<Uri[]> pendingFileChooser;
    private String pendingExport;

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setStatusBarColor(Color.rgb(21, 38, 33));
        getWindow().setNavigationBarColor(Color.rgb(21, 38, 33));

        webView = new WebView(this);
        webView.setBackgroundColor(Color.rgb(21, 38, 33));
        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(true);
        settings.setJavaScriptCanOpenWindowsAutomatically(false);
        settings.setSupportMultipleWindows(false);
        webView.addJavascriptInterface(new NativeBridge(), "PvG3Native");
        webView.setWebViewClient(new WebViewClient() {
            @Override public boolean shouldOverrideUrlLoading(
                    WebView view, WebResourceRequest request) {
                return handleNavigation(request.getUrl());
            }

            @Override public boolean shouldOverrideUrlLoading(WebView view, String url) {
                return handleNavigation(Uri.parse(url));
            }

            @Override public WebResourceResponse shouldInterceptRequest(
                    WebView view, WebResourceRequest request) {
                return serveBundledAsset(request.getUrl());
            }

            @Override public WebResourceResponse shouldInterceptRequest(
                    WebView view, String url) {
                return serveBundledAsset(Uri.parse(url));
            }
        });
        webView.setWebChromeClient(new WebChromeClient() {
            @Override public boolean onShowFileChooser(
                    WebView view, ValueCallback<Uri[]> callback,
                    WebChromeClient.FileChooserParams params) {
                return chooseTxtFile(callback);
            }
        });
        setContentView(webView);
        webView.loadUrl("https://" + ORIGIN + "/online/maker.html");
    }

    private boolean handleNavigation(Uri uri) {
        if (!"https".equals(uri.getScheme()) || !ORIGIN.equals(uri.getHost())) return true;
        if ("/online/index.html".equals(uri.getPath())) {
            finish();
            return true;
        }
        return false;
    }

    private WebResourceResponse serveBundledAsset(Uri uri) {
        if ("content".equals(uri.getScheme())) return null;
        if (!"https".equals(uri.getScheme()) || !ORIGIN.equals(uri.getHost()))
            return emptyResponse(403);
        String path = uri.getPath();
        String assetPath = null;
        if (path != null && path.startsWith("/online/")) {
            String name = path.substring("/online/".length());
            if ("maker.html".equals(name) || "maker.css".equals(name) ||
                    "maker.js".equals(name) || "maker-core.js".equals(name))
                assetPath = "online/" + name;
        } else if ("/assets/fonts/PT_Sans-Web-Regular.ttf".equals(path)) {
            assetPath = "assets/assets/fonts/PT_Sans-Web-Regular.ttf";
        }
        if (assetPath == null) return emptyResponse(404);

        try {
            InputStream stream = getAssets().open(assetPath);
            String mime = assetPath.endsWith(".html") ? "text/html" :
                    assetPath.endsWith(".css") ? "text/css" :
                    assetPath.endsWith(".js") ? "application/javascript" : "font/ttf";
            return new WebResourceResponse(mime,
                    mime.startsWith("font/") ? null : "UTF-8", stream);
        } catch (IOException error) {
            return emptyResponse(404);
        }
    }

    private WebResourceResponse emptyResponse(int status) {
        return new WebResourceResponse("text/plain", "UTF-8", status,
                status == 403 ? "Forbidden" : "Not Found",
                Collections.<String, String>emptyMap(),
                new ByteArrayInputStream(new byte[0]));
    }

    private boolean chooseTxtFile(ValueCallback<Uri[]> callback) {
        if (pendingFileChooser != null) pendingFileChooser.onReceiveValue(null);
        pendingFileChooser = callback;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_MIME_TYPES,
                new String[] {"text/plain", "application/json"});
        try {
            startActivityForResult(intent, REQUEST_OPEN_TXT);
            return true;
        } catch (RuntimeException error) {
            pendingFileChooser = null;
            callback.onReceiveValue(null);
            Toast.makeText(this, "Не удалось открыть выбор TXT-файла", Toast.LENGTH_LONG).show();
            return false;
        }
    }

    private void createTxtFile(String filename, String contents) {
        pendingExport = contents == null ? "" : contents;
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("text/plain");
        intent.putExtra(Intent.EXTRA_TITLE, filename);
        try {
            startActivityForResult(intent, REQUEST_SAVE_TXT);
        } catch (RuntimeException error) {
            pendingExport = null;
            Toast.makeText(this, "Не удалось открыть сохранение TXT-файла", Toast.LENGTH_LONG).show();
        }
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_OPEN_TXT) {
            ValueCallback<Uri[]> callback = pendingFileChooser;
            pendingFileChooser = null;
            if (callback != null) callback.onReceiveValue(
                    resultCode == RESULT_OK && data != null && data.getData() != null
                            ? new Uri[] {data.getData()} : null);
            return;
        }
        if (requestCode == REQUEST_SAVE_TXT) {
            String text = pendingExport;
            pendingExport = null;
            if (resultCode != RESULT_OK || data == null || data.getData() == null || text == null)
                return;
            try (OutputStream output = getContentResolver().openOutputStream(data.getData(), "w")) {
                if (output == null) throw new IOException("No output stream");
                output.write(text.getBytes(StandardCharsets.UTF_8));
                Toast.makeText(this, "TXT-файл уровня сохранён", Toast.LENGTH_LONG).show();
            } catch (IOException error) {
                Toast.makeText(this, "Не удалось записать TXT-файл", Toast.LENGTH_LONG).show();
            }
        }
    }

    @Override public void onBackPressed() {
        finish();
    }

    @Override protected void onDestroy() {
        if (pendingFileChooser != null) pendingFileChooser.onReceiveValue(null);
        if (webView != null) {
            webView.removeJavascriptInterface("PvG3Native");
            webView.loadUrl("about:blank");
            webView.destroy();
            webView = null;
        }
        super.onDestroy();
    }

    public final class NativeBridge {
        @JavascriptInterface public void saveTxt(String filename, String contents) {
            runOnUiThread(() -> createTxtFile(filename, contents));
        }
    }
}
