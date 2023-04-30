#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#define META_EVENT 0xFF
#define SYS_EVENT_1 0xF0
#define SYS_EVENT_2 0xF7

#define META_TABLE_LENGTH 27

typedef struct {
    uint32_t delta_time;
    uint8_t type;
    uint32_t length;
    void *data;
    char *name;
} event_t;

typedef struct event_node {
    event_t *event;
    struct event_node *next;
} event_node_t;

typedef struct {
    uint32_t length;
    uint8_t *data;
} sys_event_t;

typedef struct {
    uint8_t type;
    uint32_t length;
    uint8_t *data;
} meta_event_t;

typedef struct {
    uint8_t status;
    uint8_t data_length;
    uint8_t *data;
    char *name;
} midi_event_t;

typedef struct track {
    uint32_t length;
    event_node_t *event_list;
} track_t;

typedef struct track_node {
    track_t *track;
    struct track_node *next;
} track_node_t;

typedef struct {
    uint32_t format;
    uint32_t ticks_per_quarter_note;
    track_node_t *track_list;
} song_data_t;

const char *META_TABLE[] = {
    "Sequence Number",
    "Text Event",
    "Copyright",
    "Sequence/Track Name",
    "Instrument Name",
    "Lyric",
    "Marker",
    "Cue Point",
    "Program Name",
    "Device Name",
    "", "", "", "", "", "", "", "", "", "", "",
    "MIDI Channel Prefix",
    "MIDI Port",
    "End of Track",
    "Set Tempo",
    "SMTPE Offset",
    "Time Signature",
    "Key Signature",
    "Sequencer-Specific Meta-event"
};

song_data_t *parse_file(const char *filename) {
    assert(filename != NULL);

    // Open the MIDI file in binary mode
    FILE *file = fopen(filename, "rb");
    assert(file != NULL);

    // Get the size of the file in bytes
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Allocate memory for the song data struct
    song_data_t *song_data = malloc(sizeof(song_data_t));
    assert(song_data != NULL);

    // Copy the filename to the song data struct
    song_data->filename = malloc(strlen(filename) + 1);
    assert(song_data->filename != NULL);
    strcpy(song_data->filename, filename);

    // Parse the header chunk
    parse_header_chunk(file, song_data);

    // Parse the track chunks
    parse_track_chunks(file, song_data);

    // Check if there is any remaining data in the file
    long remaining_data = file_size - ftell(file);
    assert(remaining_data == 0);

    // Close the file
    fclose(file);

    return song_data;
}
void parse_header(FILE *fp, song_data_t *song) {
    // Read chunk type and size
    char chunk_type[5];
    uint32_t chunk_size;
    fread(chunk_type, sizeof(char), 4, fp);
    fread(&chunk_size, sizeof(uint32_t), 1, fp);
    chunk_type[4] = '\0';

    // Assert that chunk type is correct
    assert(strcmp(chunk_type, "MThd") == 0);

    // Read format, number of tracks, and division
    uint16_t format, num_tracks, division;
    fread(&format, sizeof(uint16_t), 1, fp);
    fread(&num_tracks, sizeof(uint16_t), 1, fp);
    fread(&division, sizeof(uint16_t), 1, fp);

    // Assert that format is valid
    assert(format == 0 || format == 1 || format == 2);

    // Update song data
    song->format = format;
    song->num_tracks = num_tracks;
    song->division = division;
}
void parse_track(FILE *fp, song_data_t *song) {
    // Check if file pointer and song are not null
    assert(fp != NULL && song != NULL);

    // Check if chunk type is correct
    char chunk_type[5];
    fread(chunk_type, sizeof(char), 4, fp);
    assert(strcmp(chunk_type, "MTrk") == 0);

    // Read chunk length
    uint32_t chunk_length;
    fread(&chunk_length, sizeof(uint32_t), 1, fp);
    chunk_length = be32toh(chunk_length);

    // Allocate memory for track_t and initialize it
    track_t *track = (track_t *) malloc(sizeof(track_t));
    assert(track != NULL);
    track->event_list = NULL;
    track->next_track = NULL;

    // Keep reading events until the end of the chunk
    uint32_t bytes_read = 0;
    while (bytes_read < chunk_length) {
        event_t *event = parse_event(fp);
        add_event_to_track(track, event);
        bytes_read += event->data_len + get_event_length_var_len(event->delta_time);
    }

    // Add track to song's track list
    if (song->track_list == NULL) {
        song->track_list = track;
    } else {
        track_t *curr_track = song->track_list;
        while (curr_track->next_track != NULL) {
            curr_track = curr_track->next_track;
        }
        curr_track->next_track = track;
    }
}
event_t *parse_event(FILE *fp) {
    // Read the delta time for the event
    int delta_time = read_variable_length(fp);
    
    // Read the event type
    uint8_t status_byte = fgetc(fp);
    
    // Determine the type of the event
    event_t *event = (event_t *) malloc(sizeof(event_t));
    if (status_byte == 0xFF) {
        // Meta event
        event->type = META_EVENT;
        meta_event_t *meta_event = (meta_event_t *) malloc(sizeof(meta_event_t));
        meta_event->meta_type = fgetc(fp);
        meta_event->data_len = read_variable_length(fp);
        meta_event->data = (uint8_t *) malloc(sizeof(uint8_t) * meta_event->data_len);
        fread(meta_event->data, sizeof(uint8_t), meta_event->data_len, fp);
        event->event_data.meta_event = meta_event;
    } else if (status_byte == 0xF0 || status_byte == 0xF7) {
        // System exclusive event
        event->type = (status_byte == 0xF0) ? SYS_EVENT_1 : SYS_EVENT_2;
        sys_event_t *sys_event = (sys_event_t *) malloc(sizeof(sys_event_t));
        sys_event->data_len = read_variable_length(fp);
        sys_event->data = (uint8_t *) malloc(sizeof(uint8_t) * sys_event->data_len);
        fread(sys_event->data, sizeof(uint8_t), sys_event->data_len, fp);
        event->event_data.sys_event = sys_event;
    } else {
        // MIDI event
        uint8_t event_type = status_byte >> 4;
        midi_event_t *midi_event = (midi_event_t *) malloc(sizeof(midi_event_t));
        midi_event->channel = status_byte & 0x0F;
        midi_event->type = event_type;
        midi_event->data_len = MIDI_TABLE[event_type].data_len;
        midi_event->data = (uint8_t *) malloc(sizeof(uint8_t) * midi_event->data_len);
        if (midi_event->data_len > 0) {
            fread(midi_event->data, sizeof(uint8_t), midi_event->data_len, fp);
        }
        event->type = midi_event->type;
        event->event_data.midi_event = midi_event;
    }
    
    // Set the delta time for the event
    event->delta_time = delta_time;
    
    return event;
}
sys_event_t parse_sys_event(FILE *midi_file) {
    sys_event_t sys_event;
    uint32_t data_len;

    // Read the length of the event data
    fread(&data_len, sizeof(uint32_t), 1, midi_file);

    // Allocate memory for the event data
    sys_event.data = (uint8_t*) malloc(data_len * sizeof(uint8_t));
    if (sys_event.data == NULL) {
        fprintf(stderr, "Error: Unable to allocate memory for sys_event data\n");
        exit(1);
    }

    // Read the event data into the allocated buffer
    fread(sys_event.data, sizeof(uint8_t), data_len, midi_file);

    // Set the length and type of the event
    sys_event.data_len = data_len;
    if (sys_event.data_len > 0 && sys_event.data[0] == 0xF0) {
        sys_event.type = SYS_EVENT_1;
    } else {
        sys_event.type = SYS_EVENT_2;
    }

    return sys_event;
}
meta_event_t parse_meta_event(FILE *file) {
    meta_event_t meta_event;
    uint8_t type = fgetc(file); // read the type byte

    if (type == META_END_OF_TRACK) {
        meta_event.type = META_END_OF_TRACK;
        meta_event.data = NULL;
        meta_event.data_len = 0;
        return meta_event;
    }

    uint32_t length = read_variable_length(file); // read the length of the data

    if (META_TABLE[type].fixed_length != -1) {
        // if this event has a known length, make sure it matches the expected length
        assert(length == META_TABLE[type].fixed_length);
    }

    meta_event.type = type;
    meta_event.name = META_TABLE[type].name;
    meta_event.data_len = length;
    meta_event.data = malloc(sizeof(uint8_t) * length);
    assert(meta_event.data != NULL);

    for (uint32_t i = 0; i < length; i++) {
        meta_event.data[i] = fgetc(file); // read in the data bytes
    }

    if (type == META_TEMPO_CHANGE) {
        meta_event.tempo = get_tempo_from_bytes(meta_event.data);
    }

    return meta_event;
}
midi_event_t parse_midi_event(FILE *midi_file, uint8_t status) {
    midi_event_t event;
    event.name = "MIDI Event";
    event.type = status;

    if ((status & 0x80) == 0) {
        // The status was not encoded implicitly, read the next byte as status
        event.type = status;
        status = fread_u8(midi_file);
    }

    switch (status >> 4) {
        case 0x8:
            event.name = "Note Off";
            event.data.note_off.channel = status & 0x0F;
            event.data.note_off.note = fread_u8(midi_file);
            event.data.note_off.velocity = fread_u8(midi_file);
            break;
        case 0x9:
            event.name = "Note On";
            event.data.note_on.channel = status & 0x0F;
            event.data.note_on.note = fread_u8(midi_file);
            event.data.note_on.velocity = fread_u8(midi_file);
            break;
        case 0xA:
            event.name = "Polyphonic Key Pressure";
            event.data.polyphonic_pressure.channel = status & 0x0F;
            event.data.polyphonic_pressure.note = fread_u8(midi_file);
            event.data.polyphonic_pressure.pressure = fread_u8(midi_file);
            break;
        case 0xB:
            event.name = "Control Change";
            event.data.control_change.channel = status & 0x0F;
            event.data.control_change.controller = fread_u8(midi_file);
            event.data.control_change.value = fread_u8(midi_file);
            break;
        case 0xC:
            event.name = "Program Change";
            event.data.program_change.channel = status & 0x0F;
            event.data.program_change.program = fread_u8(midi_file);
            break;
        case 0xD:
            event.name = "Channel Pressure";
            event.data.channel_pressure.channel = status & 0x0F;
            event.data.channel_pressure.pressure = fread_u8(midi_file);
            break;
        case 0xE:
            event.name = "Pitch Bend";
            event.data.pitch_bend.channel = status & 0x0F;
            event.data.pitch_bend.value = (fread_u8(midi_file) << 7) | fread_u8(midi_file);
            break;
        default:
            // Unknown MIDI event, discard data bytes and print warning
            printf("Unknown MIDI event type 0x%x\n", status >> 4);
            uint8_t data_len = fread_u8(midi_file);
            fseek(midi_file, data_len, SEEK_CUR);
            break;
    }

    return event;
}
#include <stdio.h>
#include <stdint.h>

uint32_t parse_var_len(FILE *fp) {
    uint32_t result = 0;
    uint8_t byte = 0;
    do {
        byte = fgetc(fp);
        result = (result << 7) | (byte & 0x7F);
    } while (byte & 0x80);
    return result;
}

uint16_t end_swap_16(uint8_t bytes[2]) {
    return (bytes[1] << 8) | bytes[0];
}
uint32_t end_swap_32(uint8_t bytes[4]) {
    uint32_t val = 0;
    val |= (uint32_t)bytes[3] << 0;
    val |= (uint32_t)bytes[2] << 8;
    val |= (uint32_t)bytes[1] << 16;
    val |= (uint32_t)bytes[0] << 24;
    return val;
}

uint8_t event_type(event_t *event) {
    if (event->type == META_EVENT) {
        return META_EVENT_T;
    } else if (event->type == SYS_EVENT_1 || event->type == SYS_EVENT_2) {
        return SYS_EVENT_T;
    } else {
        return MIDI_EVENT_T;
    }
}
void free_song(song_data_t *song) {
    if (song == NULL) {
        return;
    }
    track_node_t *current_track = song->track_list;
    while (current_track != NULL) {
        track_node_t *next_track = current_track->next_track;
        free_track_node(current_track);
        current_track = next_track;
    }
    free(song);
}

void free_track_node(track_node_t *track_node) {
    if (track_node == NULL) {
        return;
    }
    event_node_t *current_event = track_node->track->event_list;
    while (current_event != NULL) {
        event_node_t *next_event = current_event->next_event;
        free_event_node(current_event);
        current_event = next_event;
    }
    free(track_node->track);
    free(track_node);
}

void free_event_node(event_node_t *event_node) {
    if (event_node == NULL) {
        return;
    }
    if (event_node->event.type == SYS_EVENT_1 || event_node->event.type == SYS_EVENT_2) {
        free(event_node->event.data.sys_data);
    } else if (event_node->event.type == META_EVENT) {
        free(event_node->event.data.meta_data.data);
    }
    free(event_node);
}
