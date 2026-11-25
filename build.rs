fn main() {
    println!("cargo:rerun-if-changed=native/browser.m");
    println!("cargo:rerun-if-changed=native/Page.h");
    println!("cargo:rerun-if-changed=native/Preferences.inc");
    println!("cargo:rerun-if-changed=native/FocusColor.h");
    println!("cargo:rerun-if-changed=native/Slider.h");
    println!("cargo:rerun-if-changed=tests/native_smoke.inc");
    println!("cargo:rerun-if-changed=native/WindowToolbar.inc");
    println!("cargo:rerun-if-changed=native/Viewports.inc");
    let mut build = cc::Build::new();
    if std::env::var_os("CARGO_FEATURE_UI_TEST").is_some() {
        build.define("BROW_UI_TEST", None);
    }
    build
        .file("native/browser.m")
        .flag("-fobjc-arc")
        .flag("-mmacosx-version-min=14.0")
        .compile("brow_appkit");
    for framework in ["AppKit", "WebKit", "QuartzCore", "CoreImage"] {
        println!("cargo:rustc-link-lib=framework={framework}");
    }
}
