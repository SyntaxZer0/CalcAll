package com.syntaxzer0.calcall;

import android.app.Activity;
import android.os.Bundle;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import androidx.webkit.WebViewAssetLoader;

/*
 * CalcAll for Android is the web version (see mobile/web) running in a
 * full-screen WebView, from files inside the app, so it works offline.
 * Saved definitions, history and settings stay in the WebView's storage.
 */
public class MainActivity extends Activity {
    private WebView web;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);

        /* Serve the app's files from an https address, which WebAssembly and storage need. */
        final WebViewAssetLoader assets = new WebViewAssetLoader.Builder()
                .addPathHandler("/assets/", new WebViewAssetLoader.AssetsPathHandler(this))
                .build();

        web = new WebView(this);
        web.setBackgroundColor(0xFF181A1F);
        WebSettings settings = web.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setAllowFileAccess(false);
        web.setWebViewClient(new WebViewClient() {
            @Override
            public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
                return assets.shouldInterceptRequest(request.getUrl());
            }
        });
        setContentView(web);
        if (state != null)
            web.restoreState(state);
        else
            web.loadUrl("https://appassets.androidplatform.net/assets/www/index.html?app");
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        web.saveState(state);
    }

    @Override
    protected void onPause() {
        super.onPause();
        /* the page saves its files when it's hidden; give it the chance */
        web.evaluateJavascript("document.dispatchEvent(new Event('visibilitychange'))", null);
    }
}
