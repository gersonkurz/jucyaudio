// Theme Switcher for JucyAudio Website

(function() {
    'use strict';

    const STORAGE_KEY = 'jucyaudio-theme';
    const DEFAULT_THEME = 'orange-light';

    // Get saved theme or default
    function getSavedTheme() {
        return localStorage.getItem(STORAGE_KEY) || DEFAULT_THEME;
    }

    // Save theme preference
    function saveTheme(theme) {
        localStorage.setItem(STORAGE_KEY, theme);
    }

    // Apply theme to document
    function applyTheme(theme) {
        document.documentElement.setAttribute('data-theme', theme);

        // Update active button state
        document.querySelectorAll('.theme-button').forEach(btn => {
            btn.classList.toggle('active', btn.dataset.theme === theme);
        });
    }

    // Initialize theme on page load
    function initTheme() {
        const savedTheme = getSavedTheme();
        applyTheme(savedTheme);
    }

    // Set up theme button click handlers
    function initThemeButtons() {
        document.querySelectorAll('.theme-button').forEach(button => {
            button.addEventListener('click', function() {
                const theme = this.dataset.theme;
                applyTheme(theme);
                saveTheme(theme);
            });
        });
    }

    // Initialize when DOM is ready
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', function() {
            initTheme();
            initThemeButtons();
        });
    } else {
        initTheme();
        initThemeButtons();
    }
})();
