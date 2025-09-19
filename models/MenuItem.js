// models/MenuItem.js
const mongoose = require("mongoose");

const menuItemSchema = new mongoose.Schema({
  name: {
    type: String,
    required: true,
  },
  price: {
    type: Number,
    required: true,
    min: 0,
  },
  photo: {
    type: String, // URL to photo
    required: true,
  },
  category: {
    type: String,
    default: "General", // optional: Drinks, Starters, etc.
  }
}, { timestamps: true });

module.exports = mongoose.model("MenuItem", menuItemSchema);
