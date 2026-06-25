# -*- coding: utf-8 -*-
import os
import sys
import yaml

def extract_dict_from_config(config_path, output_txt_path):
    """从配置文件提取字典"""
    with open(config_path, 'r', encoding='utf-8') as f:
        config = yaml.safe_load(f)
    
    character_dict = config['PostProcess']['character_dict']
    
    with open(output_txt_path, 'w', encoding='utf-8') as f:
        for char in character_dict:
            f.write(char + '\n')
    
    print(f'提取字典完成，共 {len(character_dict)} 个字符')
    return output_txt_path

def model_to_header(input_path, output_path, key):
    var_name = f"{key}_model"
    if key == 'keys':
        var_name = "keys_data"

    with open(input_path, 'rb') as f:
        data = f.read()
    
    # For keys, prepend "blank\n" to the data
    if key == 'keys':
        # Read as text, prepend blank, then encode
        with open(input_path, 'r', encoding='utf-8') as f:
            text_content = f.read()
        data = ("blank\n" + text_content).encode('utf-8')

    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(f'// Auto-generated from {os.path.basename(input_path)}\n')
        f.write(f'// Model size: {len(data)} bytes\n\n')
        f.write(f'#ifndef __{var_name.upper()}_H__\n')
        f.write(f'#define __{var_name.upper()}_H__\n\n')
        f.write(f'const unsigned char {var_name}[] = {{\n')

        chunk_size = 16
        for i in range(0, len(data), chunk_size):
            chunk = data[i:i+chunk_size]
            hex_values = ', '.join(f'0x{b:02x}' for b in chunk)
            f.write(f'    {hex_values},\n')

        f.write(f'}};\n\n')
        f.write(f'const size_t {var_name}_size = {len(data)};\n\n')
        f.write(f'#endif //__{var_name.upper()}_H__\n')

    print(f'Generated: {output_path}')
    return var_name, len(data)

def extract_table_dict_from_config(config_path, output_txt_path):
    """从表格模型配置文件提取字典"""
    with open(config_path, 'r', encoding='utf-8') as f:
        config = yaml.safe_load(f)
    
    character_dict = list(config['PostProcess']['character_dict'])
    # merge_no_span_structure: 如果没有<td></td>则添加，移除<td>
    if config['PostProcess'].get('merge_no_span_structure', True):
        if "<td></td>" not in character_dict:
            character_dict.append("<td></td>")
        if "<td>" in character_dict:
            character_dict.remove("<td>")
    
    # C++ decodeOutput adds sos/eos internally, so don't include them here
    with open(output_txt_path, 'w', encoding='utf-8') as f:
        for char in character_dict:
            f.write(char + '\n')
    
    print(f'提取表格字典完成，共 {len(full_dict)} 个字符（含sos/eos）')
    return output_txt_path

def main():
    base_dir = os.path.dirname(os.path.abspath(__file__))
    models_dir = os.path.join(base_dir, 'models')
    
    # 从配置文件提取字典
    rec_config = os.path.join(models_dir, 'PP-OCRv6_small_rec_onnx', 'PP-OCRv6_small_rec_onnx.yml')
    keys_file = os.path.join(models_dir, 'ppocr_keys_v6.txt')
    if os.path.exists(rec_config):
        extract_dict_from_config(rec_config, keys_file)

    # 提取表格模型字典
    table_config = os.path.join(models_dir, 'SLANet_onnx', 'inference.yml')
    table_keys_file = os.path.join(models_dir, 'slanet_keys.txt')
    if os.path.exists(table_config):
        extract_table_dict_from_config(table_config, table_keys_file)

    # PP-OCRv6 small models with classifier
    models = {
        'det': os.path.join(models_dir, 'PP-OCRv6_small_det_onnx', 'PP-OCRv6_small_det_onnx.onnx'),
        'cls': os.path.join(models_dir, 'PP-LCNet_x1_0_textline_ori_infer.onnx'),
        'rec': os.path.join(models_dir, 'PP-OCRv6_small_rec_onnx', 'PP-OCRv6_small_rec_onnx.onnx'),
        'keys': keys_file,
        'table': os.path.join(models_dir, 'SLANet_onnx', 'inference.onnx'),
        'table_keys': table_keys_file,
    }

    output_dir = os.path.join(base_dir, '..', 'include', 'models')
    os.makedirs(output_dir, exist_ok=True)

    all_outputs = []
    total_size = 0

    for key, input_path in models.items():
        if not os.path.exists(input_path):
            print(f'Warning: {input_path} not found, skipping')
            continue

        output_path = os.path.join(output_dir, f'model_{key}.h')
        var_name, size = model_to_header(input_path, output_path, key)
        all_outputs.append((key, var_name, size, os.path.basename(input_path)))
        total_size += size

    print(f'\nTotal embedded model size: {total_size / 1024 / 1024:.2f} MB')
    print(f'Generated {len(all_outputs)} model headers')

    models_header = os.path.join(output_dir, 'embedded_models.h')
    with open(models_header, 'w', encoding='utf-8') as f:
        f.write('// Auto-generated embedded models header\n')
        f.write('// PP-OCRv6 Small models with text orientation classifier\n')
        f.write('// Include this header to use embedded models\n\n')
        f.write('#ifndef _EMBEDDED_MODELS_H_\n')
        f.write('#define _EMBEDDED_MODELS_H_\n\n')

        for key, var_name, size, filename in all_outputs:
            f.write(f'#include "model_{key}.h"\n')

        f.write('\n#endif //_EMBEDDED_MODELS_H_\n')

    print(f'Generated: {models_header}')

if __name__ == '__main__':
    main()
