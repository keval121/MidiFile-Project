#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ftw.h>
#include "library.h"
#include "midi.h"

/**
 * This file contains functions for managing a library of MIDI songs.
 * The library is implemented as a binary search tree, with each node
 * containing a song and its associated metadata.
 *
 * Author: Keval Modi
 */
typedef void (*traversal_func_t)(tree_node_t *, void *);

tree_node_t **find_parent_pointer(tree_node_t **root, char *song_name) {
    if (*root == NULL || strcmp((*root)->song->song_name, song_name) == 0) {
        return root;
    }
    
    tree_node_t **left_child = find_parent_pointer(&(*root)->left_child, song_name);
    if (left_child != NULL) {
        return left_child;
    }
    
    return find_parent_pointer(&(*root)->right_child, song_name);
}

int tree_insert(tree_node_t **root, tree_node_t *node) {
    if (*root == NULL) {
        // Empty tree, insert node as root
        *root = node;
        return INSERT_SUCCESS;
    }

    int cmp = strcmp(node->song_name, (*root)->song_name);

    if (cmp == 0) {
        // Node already present in tree
        return DUPLICATE_SONG;
    } else if (cmp < 0) {
        // Node should be inserted in left subtree
        return tree_insert(&((*root)->left_child), node);
    } else {
        // Node should be inserted in right subtree
        return tree_insert(&((*root)->right_child), node);
    }
}
int remove_song_from_tree(tree_node_t **root, char *song_name) {
    if (*root == NULL) {
        // empty tree
        return SONG_NOT_FOUND;
    }

    // check if root is the node to be removed
    if (strcmp(song_name, (*root)->song->name) == 0) {
        tree_node_t *node_to_remove = *root;
        if (node_to_remove->left_child == NULL) {
            // only right child or no child
            *root = node_to_remove->right_child;
            free_tree_node(node_to_remove);
        } else if (node_to_remove->right_child == NULL) {
            // only left child
            *root = node_to_remove->left_child;
            free_tree_node(node_to_remove);
        } else {
            // two children, find in-order successor and swap data
            tree_node_t *in_order_successor = get_in_order_successor(node_to_remove);
            song_data_t *temp = node_to_remove->song;
            node_to_remove->song = in_order_successor->song;
            in_order_successor->song = temp;
            return remove_song_from_tree(&(node_to_remove->right_child), in_order_successor->song->name);
        }
        return DELETE_SUCCESS;
    }

    // search for node to remove
    tree_node_t **parent_ptr = find_parent_pointer(root, song_name);
    if (parent_ptr == NULL || *parent_ptr == NULL) {
        // song not found
        return SONG_NOT_FOUND;
    }

    // node to remove is either left or right child of parent
    tree_node_t *node_to_remove = (*parent_ptr)->left_child;
    if (node_to_remove == NULL || strcmp(song_name, node_to_remove->song->name) != 0) {
        node_to_remove = (*parent_ptr)->right_child;
        if (node_to_remove == NULL || strcmp(song_name, node_to_remove->song->name) != 0) {
            // song not found
            return SONG_NOT_FOUND;
        }
    }

    // remove node
    if (node_to_remove->left_child == NULL) {
        // only right child or no child
        if (node_to_remove == (*parent_ptr)->left_child) {
            (*parent_ptr)->left_child = node_to_remove->right_child;
        } else {
            (*parent_ptr)->right_child = node_to_remove->right_child;
        }
        free_tree_node(node_to_remove);
    } else if (node_to_remove->right_child == NULL) {
        // only left child
        if (node_to_remove == (*parent_ptr)->left_child) {
            (*parent_ptr)->left_child = node_to_remove->left_child;
        } else {
            (*parent_ptr)->right_child = node_to_remove->left_child;
        }
        free_tree_node(node_to_remove);
    } else {
        // two children, find in-order successor and swap data
        tree_node_t *in_order_successor = get_in_order_successor(node_to_remove);
        song_data_t *temp = node_to_remove->song;
        node_to_remove->song = in_order_successor->song;
        in_order_successor->song = temp;
        return remove_song_from_tree(&(node_to_remove->right_child), in_order_successor->song->name);
    }

    return DELETE_SUCCESS;
}

void traverse_pre_order(tree_node_t *root, void *data, traversal_func_t func) {
    if (root == NULL) {
        return;
    }
    func(root, data);
    traverse_pre_order(root->left_child, data, func);
    traverse_pre_order(root->right_child, data, func);
}

void traverse_in_order(tree_node_t *root, void *data, traversal_func_t func) {
    if (root == NULL) {
        return;
    }
    traverse_in_order(root->left_child, data, func);
    func(root, data);
    traverse_in_order(root->right_child, data, func);
}

void traverse_post_order(tree_node_t *root, void *data, traversal_func_t func) {
    if (root == NULL) {
        return;
    }
    traverse_post_order(root->left_child, data, func);
    traverse_post_order(root->right_child, data, func);
    func(root, data);
}
void free_node(tree_node_t *node) {
    if (node == NULL) {
        return;
    }
    
    free_song(node->song); // Free the song associated with the node
    free_node(node->left_child); // Free the left child subtree
    free_node(node->right_child); // Free the right child subtree
    
    free(node); // Free the node itself
}
void print_node(tree_node_t *node, FILE *fp) {
    fprintf(fp, "%s\n", node->song->song_name);
}

void free_library(tree_node_t *root) {
    if (root == NULL) {
        return;
    }
    free_library(root->left_child);
    free_library(root->right_child);
    free_node(root);
}
void make_library(const char *dir_name) {
    assert(dir_name != NULL);
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(dir_name)) != NULL) {
        /* Loop through all files and directories in the given directory */
        while ((ent = readdir(dir)) != NULL) {
            /* Check if the entry is a directory and not "." or ".." */
            if (ent->d_type == DT_DIR && strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
                /* Recursively call make_library on the subdirectory */
                char sub_dir_name[PATH_MAX];
                snprintf(sub_dir_name, sizeof(sub_dir_name), "%s/%s", dir_name, ent->d_name);
                make_library(sub_dir_name);
            }
            /* Check if the entry is a file and has a ".mid" extension */
            else if (ent->d_type == DT_REG && strstr(ent->d_name, ".mid") != NULL) {
                /* Extract the song name from the file path */
                char *song_name = strrchr(ent->d_name, '/');
                if (song_name == NULL) {
                    song_name = ent->d_name;
                } else {
                    song_name++;
                }
                /* Create a new song and add it to the library */
                char full_path[PATH_MAX];
                snprintf(full_path, sizeof(full_path), "%s/%s", dir_name, ent->d_name);
                song_t *song = new_song(song_name, full_path);
                int insert_result = tree_insert(&g_song_library, new_tree_node(song));
                if (insert_result == DUPLICATE_SONG) {
                    fprintf(stderr, "Warning: duplicate song '%s' found in library\n", song_name);
                    free_song(song);
                }
            }
        }
        closedir(dir);
    } else {
        /* Failed to open directory */
        perror("make_library opendir");
    }
}

