##################################################################################
# File: sdout.py — SDOut Package Builder
#
# Description:
#   This script automates the creation of `sdout.zip`, a complete deployment
#   package for Ultrahand Overlay. It downloads, organizes, and assembles
#   Ultrahand Overlay components, nx-ovlloader, and any additional files into
#   a proper SD card structure ready for use.
#
#   The script automatically creates the required folder structure, downloads
#   and extracts dependencies, organizes their contents, and packages everything
#   into `sdout.zip` — which can be extracted directly to the root of an SD card.
#
# Related Projects:
#   - Ultrahand Overlay: https://github.com/ppkantorski/Ultrahand-Overlay
#   - nx-ovlloader: https://github.com/ppkantorski/nx-ovlloader
#
#   For the latest updates or to contribute, visit the GitHub repository:
#   https://github.com/ppkantorski/Ultrahand-Overlay
#
# Note:
#   This notice is part of the official project documentation and must not
#   be altered or removed.
#
# Requirements:
#   - Python 3.6+
#   - requests library (`pip install requests`)
#   - `ovlmenu.ovl` file in the script directory
#
# Licensed under GPLv2
# Copyright (c) 2025 ppkantorski
##################################################################################

import os
import shutil
import zipfile
import requests
from pathlib import Path
import tempfile

def download_file(url, destination):
    """Download a file from URL to destination"""
    print(f"Downloading {url}...")
    response = requests.get(url, stream=True)
    response.raise_for_status()
    
    with open(destination, 'wb') as f:
        for chunk in response.iter_content(chunk_size=8192):
            f.write(chunk)
    print(f"Downloaded to {destination}")

def extract_zip(zip_path, extract_to, exclude_metadata=True):
    """Extract zip file, optionally excluding metadata files"""
    print(f"Extracting {zip_path}...")
    with zipfile.ZipFile(zip_path, 'r') as zip_ref:
        for member in zip_ref.namelist():
            # Skip metadata files like ._ files and __MACOSX
            if exclude_metadata:
                if member.startswith('__MACOSX') or '._' in member:
                    continue
            zip_ref.extract(member, extract_to)
    print(f"Extracted to {extract_to}")

def create_zip_without_metadata(source_dir, output_zip):
    """Create a zip file excluding metadata files"""
    print(f"Creating {output_zip}...")
    with zipfile.ZipFile(output_zip, 'w', zipfile.ZIP_DEFLATED) as zipf:
        for root, dirs, files in os.walk(source_dir):
            for file in files:
                # Skip metadata files
                if file.startswith('._') or file == '.DS_Store':
                    continue
                
                file_path = os.path.join(root, file)
                arcname = os.path.relpath(file_path, source_dir)
                zipf.write(file_path, arcname)
    print(f"Created {output_zip}")

def main():
    script_dir = Path.cwd()
    sdout_dir = script_dir / "breezehand"
    sdout_zip = script_dir / "breezehand.zip"
    temp_dir = tempfile.mkdtemp()
    
    try:
        # Clean up any existing sdout folder and zip file
        print("Cleaning up previous builds...")
        if sdout_dir.exists():
            shutil.rmtree(sdout_dir)
            print("Deleted existing sdout folder")
        if sdout_zip.exists():
            sdout_zip.unlink()
            print("Deleted existing sdout.zip")
        
        # Step 1: Create sdout folder structure
        print("Creating folder structure...")
        folders = [
            "config/breezehand",
            "config/breezehand/downloads",
            "config/breezehand/flags",
            "config/breezehand/lang",
            "config/breezehand/notifications",
            "config/breezehand/payloads",
            "config/breezehand/sounds",
            "config/breezehand/themes",
            "config/breezehand/wallpapers",
            "switch/.overlays",
            "switch/.packages"
        ]
        
        for folder in folders:
            folder_path = sdout_dir / folder
            folder_path.mkdir(parents=True, exist_ok=True)
            print(f"Created {folder_path}")
        
        # Step 2: Download and extract nx-ovlloader.zip
        ovlloader_zip = Path(temp_dir) / "nx-ovlloader.zip"
        download_file(
            "https://github.com/ppkantorski/nx-ovlloader/releases/latest/download/nx-ovlloader.zip",
            ovlloader_zip
        )
        extract_zip(ovlloader_zip, sdout_dir)
        
        # Step 3: Download and process Ultrahand-Overlay
        ultrahand_zip = Path(temp_dir) / "ultrahand-main.zip"
        ultrahand_temp = Path(temp_dir) / "ultrahand_temp"
        
        download_file(
            "https://github.com/ppkantorski/Ultrahand-Overlay/archive/refs/heads/main.zip",
            ultrahand_zip
        )
        extract_zip(ultrahand_zip, ultrahand_temp)
        
        # Find the extracted folder (it will be Ultrahand-Overlay-main)
        extracted_folders = [f for f in ultrahand_temp.iterdir() if f.is_dir()]
        if not extracted_folders:
            raise Exception("Could not find extracted Ultrahand folder")
        
        ultrahand_root = extracted_folders[0]
        
        # Step 4: Copy lang files (Prefer local)
        lang_dest = sdout_dir / "config/breezehand/lang"
        lang_local = script_dir / "lang"
        lang_source = ultrahand_root / "lang"
        
        # Copy from repo first, then overwrite with local if exists
        if lang_source.exists():
            print("Copying language files from repo...")
            for json_file in lang_source.glob("*.json"):
                shutil.copy2(json_file, lang_dest)
        
        if lang_local.exists():
            print("Applying local language files...")
            for json_file in lang_local.glob("*.json"):
                shutil.copy2(json_file, lang_dest)
                print(f"Updated {json_file.name}")
        
        # Step 5: Copy ultrahand_updater.bin (Prefer local)
        payload_dest = sdout_dir / "config/breezehand/payloads"
        payload_local = script_dir / "payloads/ultrahand_updater.bin"
        payload_source = ultrahand_root / "payloads/ultrahand_updater.bin"
        
        if payload_local.exists():
            print("Copying local payload file...")
            shutil.copy2(payload_local, payload_dest)
        elif payload_source.exists():
            print("Copying payload file from repo...")
            shutil.copy2(payload_source, payload_dest)
        
        # Step 6: Copy theme files (Prefer local)
        theme_dest = sdout_dir / "config/breezehand/themes"
        theme_local = script_dir / "themes"
        theme_source = ultrahand_root / "themes"
        theme_files = ["ultra.ini", "ultra-blue.ini"]
        
        # Helper to copy from specific source
        def copy_themes(source):
            if source.exists():
                for theme_file in theme_files:
                    theme_file_path = source / theme_file
                    if theme_file_path.exists():
                        shutil.copy2(theme_file_path, theme_dest)
                        print(f"Copied theme: {theme_file}")

        print("Processing themes...")
        copy_themes(theme_source)
        copy_themes(theme_local) # Local overwrites repo
        
        # Step 7: Copy sound files (Prefer local)
        sounds_dest = sdout_dir / "config/breezehand/sounds"
        sounds_local = script_dir / "sounds"
        sounds_source = ultrahand_root / "sounds"
        
        def copy_sounds(source):
            if source.exists():
                for wav_file in source.glob("*.wav"):
                    shutil.copy2(wav_file, sounds_dest)
        
        print("Processing sounds...")
        copy_sounds(sounds_source)
        copy_sounds(sounds_local) # Local overwrites repo
        
        # Step 8: Copy cheat_url_txt.template
        print("Copying cheat_url_txt.template...")
        template_source = script_dir / "cheat_url_txt.template"
        template_dest = sdout_dir / "config/breezehand/cheat_url_txt.template"
        
        if template_source.exists():
            shutil.copy2(template_source, template_dest)
            print(f"Copied cheat_url_txt.template")
        else:
            print("Warning: cheat_url_txt.template not found in script directory")
            
        # Step 9: Copy breezehand.ovl
        print("Copying breezehand.ovl...")
        ovlmenu_source = script_dir / "breezehand.ovl"
        ovlmenu_dest = sdout_dir / "switch/.overlays"
        
        if ovlmenu_source.exists():
            shutil.copy2(ovlmenu_source, ovlmenu_dest)
            print(f"Copied breezehand.ovl")
        else:
            print("Warning: breezehand.ovl not found in script directory")
        
        # Step 10: Clean up temporary files
        print("Cleaning up temporary files...")
        shutil.rmtree(temp_dir)
        print("Temporary files deleted")
        
        # Step 11: Create final zip
        output_zip = script_dir / "breezehand.zip"
        create_zip_without_metadata(sdout_dir, output_zip)
        
        print(f"[DONE] Successfully created breezehand.zip!")
        print(f"Location: {output_zip}")
        
    except Exception as e:
        print(f"\n[ERROR] Error: {e}")
        # Clean up on error
        if Path(temp_dir).exists():
            shutil.rmtree(temp_dir)
        raise
    
if __name__ == "__main__":
    main()
